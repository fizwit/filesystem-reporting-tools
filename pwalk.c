/*
 *  pwalk.c  Parrallel Walk a file system and report file meta data

Copyright (C) (2013-2016) John F Dey

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include "pwalk.h"

static char *whoami = "pwalk";
static char *Version = "3.0.0 Jul 14 2020 John F Dey john@fuzzdog.com";

// 3.0.0 Major feature Change - use a function pointer to call generic file
//        processing functions. Separate the traversal code from the file
//        operations code into a new file:  fileProcess.c

// 2.6.10 pino used wrong type, improve error message for lstat
//        improve output format for inodes

// 2.6.9 Oct 27 2018 Add header to to CSV as option
// 2.6.8 Oct 27 2017 depth feature
// 2.6.7 May 31 2017 exclude feature
// static char *Version = "2.6.4 Dec 12 2015 John F Dey john@fuzzdog.com";
// static char *Version = "2.6.3 Dec 9 2015 John F Dey john@fuzzdog.com";
// static char *Version = "2.6.2 Aug 7 2015 John F Dey john@fuzzdog.com";

#define MAXEXFILES 512
char *exclude_list[MAXEXFILES];

#define MAXTHRDS 32
int ThreadCNT  = 1; /* ThreadCNT < MAXTHRDS */
int totalTHRDS =0;
struct threadData tdslot[MAXTHRDS];
pthread_mutex_t mutexFD;
pthread_mutex_t mutexFileProcess;
pthread_mutex_t mutexDirProcess;

/* conditioanally change file ownership --chown_from --chown_to */
uid_t UID_orig, UID_new;
gid_t GID_new;
int chown_flag =0;

/* function prototypes */
void (*fileProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);
void (*dirProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);
void csv_escape(char *in, char *out);
void *fileDir( void *arg );
int check_exclude_list(char *fname);
void get_exclude_list(char *fname, char *list[]);
void changeOwner( struct threadData *cur, char *exten, struct stat *f,
        long fileCnt, /* directory only - count files in directory */
        long dirSz );  /* directory only - sum of files within directory */
void printVersion(char *whoami, char *Version);
void add_exclude_name(char *name);

void
printHeader()
{
   printf("inode,parent-inode,directory-depth,\"filename\"");
   printf(",\"fileExtension\",UID,GID,st_size,st_dev,st_blocks" );
   printf(",st_nlink,\"st_mode\",st_atime,st_mtime,st_ctime,pw_fcount");
   printf(",pw_dirsum\n");
}

void
printHelp()
{
   printf("Useage : %s (fully qualified file name)\n", whoami);
   printf("Flags: --help --version \n" );
   printf("       --NoSnap Ignore directories with name .snapshot\n");
   printf("       --exclude filename <file> contains a list of");
   printf(" directories \n");
   printf("         to exclude from reporting\n");
   printf("       --header write CSV header with output\n");
   printf("Conditionally Change File Owner. Two Flags are required.\n");
   printf("       --chown_from UID\n");
   printf("       --chown_to UID:GID\n\n");
   printf("Each line of output represents one file. st_* fields are direct ");
   printf("from the inode\ndata structure. pwalk provides additional ");
   printf("data for directories.\n\n");
   printf(" - directory-depth: Values are incremented by directory depth. ");
   printf("Initial root\n   directory has value of -1. ");
   printf("Files in the root directory have value 0.\n");
   printf(" - pw_fcount: Number of files in a directory. Value is -1 ");
   printf("if file is not a directory\n" );
   printf(" - pw_dirsum: Sum of file sizes in single directory. Value ");
   printf("of -1 if\n   file is not a directory\n\n");
   printf("File Header:\n");
   printHeader();
}

/*
 *  printStat this needs to be in a crital secion  (and it is!)
 */
void
printStat( struct threadData *cur, char *exten, struct stat *f,
        long fileCnt, /* directory only - count files in directory */
        long dirSz )  /* directory only - sum of files within directory */
{
   char out[FILENAME_MAX+FILENAME_MAX];
   char fname[FILENAME_MAX];
   char exten_csv[FILENAME_MAX];
   ino_t ino, pino;
   long depth;

   csv_escape(cur->dname, fname);
   if ( exten )
      csv_escape(exten, exten_csv);
   else
      exten_csv[0] = '\0';
   if ( fileCnt != -1 ) {  /* directory */
      ino = f->st_ino; pino = cur->pinode; depth = cur->depth - 1;}
   else {  /* Not a directory */
      ino = f->st_ino; pino = cur->pstat.st_ino; depth = cur->depth; }
   sprintf ( out, "%ju,%ju,%ld,\"%s\",\"%s\",%ld,%ld,%ld,%ld,%ld,%d,\"%07o\",%ld,%ld,%ld,%ld,%ld\n",
            (uintmax_t)ino, (uintmax_t)pino, depth,
            fname, exten_csv, (long)f->st_uid,
            (long)f->st_gid, (long)f->st_size, (long)f->st_dev,
            (long)f->st_blocks, (int)f->st_nlink,
            (int)f->st_mode,
            (long)f->st_atime, (long)f->st_mtime, (long)f->st_ctime,
            fileCnt, dirSz );
    fputs( out, stdout );
}

int
main( int argc, char* argv[] )
{
    int error, i, colon =':';
    char *gid_ptr;
    int rootfd;
    struct stat root;

    if ( argc < 2 ) {
        printHelp( );
        exit( EXIT_FAILURE );
    }
    exclude_list[0] = NULL;
    argc--; argv++;
    while ( argc > 0 && *argv[0] == '-' ) {
        if ( !strcmp(*argv, "--NoSnap" ) )
           add_exclude_name(".snapshot");
        if ( !strcmp(*argv, "--help" ) ) {
           printHelp( );
           exit(0); }
        if ( !strcmp(*argv, "--version" ) || !strcmp(*argv, "-v") ) {
            printVersion(whoami, Version);
            exit(0); }
        if ( !strcmp(*argv, "--header" ) || !strcmp(*argv, "-v") )
           printHeader();
        if ( !strcmp(*argv, "--exclude" )) {
           argc--; argv++;
           get_exclude_list(*argv, exclude_list);}
        if ( !strcmp(*argv, "--chown_from")) {
           argc--; argv++;
           UID_orig = atoi(*argv);
           chown_flag++;
        }
        if ( !strcmp(*argv, "--chown_to")) {
           argc--; argv++;
           UID_new = atoi(*argv);
           if ( gid_ptr = strchr(*argv, colon))
              GID_new = atoi(++gid_ptr);
           else {
              fprintf( stderr, "--chown_to requires UID:GID as argument\n");
              exit(1);
           }
           chown_flag++;
        }
        argc--; argv++;
    }
    fileProcess = &printStat;
    dirProcess = &printStat;
    if ( chown_flag == 2 ) {
       fprintf(stderr, "chown UID_orig: %d  UID_new: %d GID_new: %d\n", (int)UID_orig, (int)UID_new, (int)GID_new);
       fileProcess = &changeOwner;
    }
    for ( i=0; i<MAXTHRDS; i++ ) {
        tdslot[i].THRDid = -1;
        if ( (error = pthread_attr_init( &tdslot[i].tattr )) )
            fprintf( stderr, "Failed to create pthread attr: %s\n",
                             strerror(error));
        else if ( (error = pthread_attr_setdetachstate( &tdslot[i].tattr,
                             PTHREAD_CREATE_DETACHED)
                  ) )
            fprintf( stderr, "failed to set attribute detached: %s\n",
                             strerror(error));
    }
    pthread_mutex_init(&mutexFD, NULL);
    pthread_mutex_init(&mutexFileProcess, NULL);
    pthread_mutex_init(&mutexDirProcess, NULL);

    if ((rootfd = open(*argv, O_DIRECTORY | O_RDONLY)) == -1 ) {
        fprintf( stderr, "Could not open root directory:'%s' %s\n", *argv, strerror(errno));
        exit(errno);
    }
    strcpy( tdslot[0].dname, (const char*) *argv );
    
    memcpy( &tdslot[0].pstat, &root, sizeof( struct stat ) );
    tdslot[0].dirfd = rootfd;
    tdslot[0].THRDid = totalTHRDS++; /* first thread is zero */
    tdslot[0].flag = 0;
    tdslot[0].depth = 0;
    tdslot[0].pinode = 0;
    pthread_create( &(tdslot[0].thread_id), &tdslot[0].tattr, fileDir,
                    (void*)&tdslot[0] );
    pthread_exit( NULL );
}
