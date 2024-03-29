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
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include "pwalk.h"

/* #define THRD_DEBUG */

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

int SNAPSHOT =0; /* if set ignore directories called .snapshot */
int DEPTH = 0; /* if set do not traverse beyond directory depth */
int ONE_FS =0; /* skip directories on different file systems -x */
dev_t ST_DEV;  /* save st_dev of root file */

#define MAXTHRDS 32
int ThreadCNT  = 1; /* ThreadCNT < MAXTHRDS */
int totalTHRDS =0;
struct threadData tdslot[MAXTHRDS];
pthread_mutex_t mutexFD;
pthread_mutex_t mutexPrintStat;

int check_exclude_list(char *fname);
void verify_paths(char *list[]);
void get_exclude_list(char *fname, char *list[]);

/* conditioanally change file ownership --chown_from --chown_to */
uid_t UID_orig, UID_new;
gid_t GID_new;
int chown_flag =0;

/* Process files */
void
(*fileProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);

/*
 * conditionally change file ownership
 * if file owned by UID_orig chown UID_new:GID_new
 */
void
changeOwner( struct threadData *cur, char *exten, struct stat *f,
        long fileCnt, /* directory only - count files in directory */
        long dirSz );  /* directory only - sum of files within directory */
/*
 *  printStat this needs to be in a crital secion  (and it is!)
 */
void
printStat( struct threadData *cur, char *exten, struct stat *f,
        long fileCnt, /* directory only - count files in directory */
        long dirSz );  /* directory only - sum of files within directory */


void
printVersion( ) {
   fprintf(stderr, "%s version %s\n", whoami, Version );
   fprintf(stderr, "%s Copyright (C) 2013 John F Dey\n", whoami );
   fprintf(stderr, "pwalk comes with ABSOLUTELY NO WARRANTY;\n" );
   fprintf(stderr, "This is free software, you can redistribute it and/or\n");
   fprintf(stderr, "modify it under the\nterms of the GNU General Public");
   fprintf(stderr, " License as published by the Free Software Foundation;\n");
   fprintf(stderr, "either version 2 of the License, or (at your option) any");
   fprintf(stderr, " later version.\n\n" );
}

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
   printf("       --depth n Stop walking when (n) depth is reached\n");
   printf("       --NoSnap Ignore directories with name .snapshot\n");
   printf("       --exclude filename <file> contains a list of");
   printf(" directories \n");
   printf("         to exclude from reporting\n");
   printf("       --one-file-system skip directories on different file");
   printf(" systems\n");
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

/********************************
    Open a directory and read the conents.
    call opendir with path passed in as an argument
    stat every file from opendir

    If maxthread is not reached creat a new thread and call self
    If no threads available Recursively call self for each directory
    from opendir.

    print inode meta data for each file, one line per file in CSV format
    print directory information after every file is processed from
    open dir.  Direcory information has - count of files, sum of file sizes

*********************************/
void
*fileDir( void *arg )
{
    char *s, *t, *u, *dot, *end_dname;
    int  i, slot, status;
    DIR *dirp;
    long localCnt =0; /* number of files in a specific directory */
    long localSz  =0; /* byte cnt of files in the local directory 2010.07 */
    struct dirent *d;
    struct stat f;
    struct threadData *cur, *new, local;

    cur = (struct threadData *) arg;
#ifdef THRD_DEBUG
    fprintf( stderr, "msg=fileDir,threadID=%ld,rdepth=%d,file=%s\n",
        cur->THRDid, cur->flag, cur->dname );
#endif /* THRD_DEBUG */
    if ( (dirp = opendir( cur->dname )) == NULL ) {
        fprintf( stderr, "Locked Dir: %s\n", cur->dname );
        return arg;
    }
    /* find the end of fs->name and put '/' at the end <end_dname>
       points to char after '/' */
    s = cur->dname + strlen(cur->dname);
    *s++ = '/';
    end_dname = s;
    while ( (d = readdir( dirp )) != NULL ) {
        if ( strcmp(".",d->d_name) == 0 ) continue;
        if ( strcmp("..",d->d_name) == 0 ) continue;
        localCnt++;
        s = d->d_name; t = end_dname;
        while ( *s )  /* copy file name to end of current path */
            *t++ = *s++;
        *t = '\0';
        if ( lstat ( cur->dname, &f ) == -1 ) {
            fprintf( stderr, "threadID=%ld,rdepth=%d lstat: '%s' %s\n",
              cur->THRDid, cur->flag, strerror(errno), cur->dname);
            continue;
        }
        /* don't report data from foreign file systems */
        if ( ONE_FS && f.st_dev != ST_DEV )
            continue;
        /* Follow Sub dirs recursivly but don't follow links */
        localSz += f.st_size;
        if ( S_ISDIR(f.st_mode) ) {
            if ( SNAPSHOT && !strcmp( ".snapshot", d->d_name ) )
               continue; /* next file from readdir */
            if ( DEPTH && DEPTH == cur->depth )
               continue; /* don't do any deeper than this */
            if ( exclude_list[0] && check_exclude_list(cur->dname) )
                    continue;
            pthread_mutex_lock (&mutexFD);
            if ( ThreadCNT < MAXTHRDS ) {
                slot = 0;
                while ( slot < MAXTHRDS ) {
                    if ( tdslot[slot].THRDid == -1 ) {
                        new = &tdslot[slot];
                        new->THRDid = totalTHRDS++;
                        new->flag = 0;   /* recurse flag reset for new thread */
                        break;
                    }
                    slot++;
                }
                if ( slot == MAXTHRDS )  { /* this would be bad */
                   fprintf( stderr, "error=%s,threadID=%ld,rdepth=%d,ThreadCNT=%d\n",
                   "\"no available threads\"", cur->THRDid, cur->flag, ThreadCNT );
                   exit( 1 );
                }
                ThreadCNT++; /* allocate the thread */
            } else {
                new = &local;
                new->THRDid = cur->THRDid;
                new->flag = cur->flag + 1;
            }
            pthread_mutex_unlock (&mutexFD);
            /* create ponter to tdslot that will be used for next
               call to fileDir - local or from array - shorten next block of
               code
             */
            memcpy( &(new->pstat), &f, sizeof( struct stat ) );
            strcpy( new->dname, (const char*)cur->dname );
            new->depth  = cur->depth + 1;
            new->pinode = cur->pstat.st_ino; /* Parent Inode */
            if ( new->THRDid != cur->THRDid ) {  /* new thread available */
                pthread_create( &tdslot[slot].thread_id, &tdslot[slot].tattr,
                                fileDir, (void*)new );
            } else {
                fileDir( (void*) new );
            }
        } else {
           s = end_dname + 1; dot = NULL; /* file extension */
           while ( *s ) {
               if (*s == '.') dot = s+1;
               s++;
           }
           pthread_mutex_lock (&mutexPrintStat);
           (*fileProcess)( cur, dot, &f, (long)-1, (long)0 );
           pthread_mutex_unlock (&mutexPrintStat);
        }
    }
    closedir( dirp );
    *--end_dname = '\0';
    s = end_dname - 1; dot = NULL;
    while ( *s != '/' ) {
       if (*s == '.') { dot = s+1; break; }
       s--; }
    if ( s+1 == dot ) /* Dot file is not an extension Exp: /.bashrc */
       dot = NULL;
    pthread_mutex_lock (&mutexPrintStat);
    (*fileProcess)( cur, dot, &cur->pstat, localCnt, localSz);
    pthread_mutex_unlock (&mutexPrintStat);
    if ( cur->flag == 0 ) { /* this instance of fileDir is a thread */
        pthread_mutex_lock ( &mutexFD );
#ifdef THRD_DEBUG
        fprintf( stderr, "msg=endTHRD,threadID=%ld,rdepth=%d,file=<%s>\n",
        cur->THRDid, cur->flag, cur->dname );
#endif
        --ThreadCNT;
        cur->THRDid = -1;
        pthread_mutex_unlock ( &mutexFD );
        pthread_exit( EXIT_SUCCESS );
    }
    /* else return ; */
#ifdef THRD_DEBUG
    fprintf( stderr, "msg=endRecurse,threadID=%ld,rdepth=%d,file=<%s>\n",
        cur->THRDid, cur->flag, cur->dname );
#endif /* THRD_DEBUG */
}

int
main( int argc, char* argv[] )
{
    int error, i, colon =':';
    char *s, *c, *gid_ptr;
    struct stat root;

    if ( argc < 2 ) {
        printHelp( );
        exit( EXIT_FAILURE );
    }
    exclude_list[0] = NULL;
    argc--; argv++;
    while ( argc > 0 && *argv[0] == '-' ) {
        if ( !strcmp(*argv, "--NoSnap" ) )
           SNAPSHOT = 1;
        if ( !strcmp(*argv, "--depth" ) ) {
           argc--; argv++;
           DEPTH = atoi(*argv);
        }
        if ( !strcmp(*argv, "--help" ) ) {
           printHelp( );
           exit(0); }
        if ( !strcmp(*argv, "--version" ) || !strcmp(*argv, "-v") )
           printVersion( );
        if ( !strcmp(*argv, "--header" ) || !strcmp(*argv, "-v") )
           printHeader();
        if ( !strcmp(*argv, "--exclude" )) {
           argc--; argv++;
           get_exclude_list(*argv, exclude_list);
           verify_paths(exclude_list); }
        if ( !strcmp(*argv, "--one-file-system" ) || !strcmp(*argv, "-x") )
           ONE_FS = 1;
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
    if (setuid((uid_t) 0)) {
       fprintf(stderr, "unable to setuid root; not all files will be processed\n");
    }
    fileProcess = &printStat;
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
    pthread_mutex_init(&mutexPrintStat, NULL);

    strcpy( tdslot[0].dname, (const char*) *argv );
    if ( lstat( *argv, &root ) == -1 ) {
        fprintf( stderr, "lstat: '%s' %s\n", *argv, strerror(errno));
        exit(errno);
    }
    ST_DEV = root.st_dev;
    memcpy( &tdslot[0].pstat, &root, sizeof( struct stat ) );
    tdslot[0].THRDid = totalTHRDS++; /* first thread is zero */
    tdslot[0].flag = 0;
    tdslot[0].depth = 0;
    tdslot[0].pinode = 0;
    pthread_create( &(tdslot[0].thread_id), &tdslot[0].tattr, fileDir,
                    (void*)&tdslot[0] );
    pthread_exit( NULL );
}
