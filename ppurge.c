/*
ppurge.c  Parrallel Walk a file system and remove old files

Copyright (C) (2023) John F Dey

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; GPL version 3

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>.

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
#include <fcntl.h>
#include <utime.h>
#include "pwalk.h"

/*
ppurge  Parallel Purge

ppurge is a tool for maintaining HPC scratch storage volumes by removing files
past a certain age.
File purging is accomplished with two steps. Step one is to mark a file as
"purgable" by moving the file into a temporary cache directory named .ppurge.
Ppurge subdirectories are local to the directory where the data files reside.
The files in `.ppurge` are kept for an additional n days until removed permanently.

The example usage case are volumes attached to HPC systems which have names like
"/scratch30". Ppurge should be run once per day. Files are only removed from the
volme after n+n days. The first `n` days the files are moved to `.ppurge`. Then after
an additional `n` days the files are removed (unlink).

Ppurge should only run on volumes that do not have snapshots. The storage volume
should be a single volume that is not bridged.

 - File age is based on `mtime`.
 - `.ppurge` directories only contain files, no directories are removed.
 - mtime and atime are not affected by move (rename)
 - Remove files from the `.ppurge` cache based on `mtime`
 - during the cache period users can recover files from `.ppurge`
   by using the linux "move" command. Do not let users use "copy" cp.
   mv .ppurge/I_need_this_file .
 - .ppurge directory has user sticky bit set. Only the file owners can move or delete files

When the .ppurge directory is empty it will be removed.

At present Ppurge does not remove directories. There are many structal
directories in scatch systems which could be removed due to in activity.
But not removing directories will leave many empty directory trees in
scratch file systems. A feature for Deleting directories could be implemented
with a directory level test. For scratch volumes with well defined directory
structure. '/scratch30/department/user/project/Purge at this level'
if DEPTH > 4 purge directories

Output is written to stdout. Output is a list of all files that are purged or removed.
Output is wirtten in CSV format. First character of each line is 'P' or 'R', for Purged or Removed.

Format of output:
type, depth, fname, UID, GID, st_size, st_mode, atime, mtime, ctime

ppurge creates a log file with the following name ppurge-YYYY.MM.DD-HH_MM_SS.log
Internal error messages are written to the log file.

A list of path names with illegal characters are written to the log file.
*/

static char *whoami = "ppurge";
static char *Version = "0.1.0 Aug 14 2023 John F Dey john@fuzzdog.com";

/*
 0.1.0  Initial version. Code base copied from pwalk. Purging and reporting
        seems to difficult to perform in one walk of the tree. Purging
        will be a dedicated process.
*/


FILE *Logfd;   /* error log */
time_t Ptime;  /* Purge all files older than this time stamp (less than)*/
time_t Rtime;  /* Remove all files older than this time stamp (Ptime * 2) */
int DEPTH = 0; /* possible furture use for directory purging */


int ThreadCNT  = 1; /* ThreadCNT < MAXTHRDS */
int totalTHRDS = 0;
struct threadData tdslot[MAXTHRDS];
pthread_mutex_t mutexFD;
pthread_mutex_t mutexFileProcess;
pthread_mutex_t mutexDirProcess;

/* function prototypes */
void
(*fileProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);
void *fileDir( void *arg );
int check_exclude_list(char *fname);
void verify_paths(char *list[]);
void get_exclude_list(char *fname, char *list[]);
void printVersion(char *whoami, char *Version);
void csv_escape(char *in, char *out);

void
printHelp()
{
    printf("Useage : %s (fully qualified path )\n", whoami);
    printf("ppurge should be run daly on volumes with the same value for purgeDays\n");
    printf("Flags: --help\n       --version\n" );
    printf("       --purgeDays (positive integer) Purge files older than n days.\n");
}

/*  called from fileDIR() to process directories
    protected with <mutexDirProcess>
*/
void purgeDir(struct threadData *cur, char *exten, struct stat *f, long, long)
{
    struct timespec purgeDir_atime;

    if ( !strcmp(".ppurge", d->d_name)) {
        if (purgedir_fd == -1) {
            purgedir_fd = openat(cur->dirfd, ".ppurge", O_RDONLY);
            purgedir_atime = f.st_atime;
        }
    }
}

void
(*purgeFile)(struct threadData *cur, char *exten, struct stat *f, long, long)
{
    int ret;

    if (f.st_mtime <= (time_t)0 || f.st_atime <= (time_t)0) { // BeeGFS issue with empty mtime
        fprintf(Logfd, "bad mtime: %s\n", cur->dname);
    if ((ret = utimensat(cur->dirfd, d->d_name, NULL, 0)) != 0)
        fprintf(Logfd, "utimes fail: %s\n", cur->dname);
    continue;
            }
            if ( (f.st_mode & S_IFMT) == S_IFLNK) {
                DEBUG_1("link:%s\n", cur->dname);
                continue;
            }
            if ( f.st_mtime < Ptime) {
                DEBUG_1("purge: %s\n", cur->dname);
                if ( purgedir_fd == -1 )
                    purgedir_fd = create_ppurge(cur->dirfd, &purgedir_atime);
                if (renameat(cur->dirfd, d->d_name, purgedir_fd, d->d_name) == -1) {
                    fprintf(Logfd, "BADNESS %s could not be moved to .ppurge: %s\n", cur->dname, strerror(errno));
                } else {
                    // need full path name for csv output
                    purgeLog( cur, 'P', &f);
                }
            } else
                fcount +=1; }

/*
 *  purgeLog
 *  log files that are moved to purge, and files that are removed.
 *  Initial release of ppurge only purges file. Asume no directories are purged.
 *
 */
void
purgeLog( struct threadData *cur, char type, struct stat *f)
{
   char out[FILENAME_MAX+FILENAME_MAX];
   char fname[FILENAME_MAX];
   long depth;

   csv_escape(cur->dname, fname);
   depth = cur->depth;
   sprintf ( out, "%c,%ld,\"%s\",%ld,%ld,%ld,\"%07o\",%ld,%ld,%ld\n",
            type, depth, fname, (long)f->st_uid, (long)f->st_gid, (long)f->st_size, (int)f->st_mode,
            (long)f->st_atime, (long)f->st_mtime, (long)f->st_ctime);
    fputs( out, stdout );
}

/********************************
    opendir .ppurge directory
    if mtime < PPURGE_tm remove the file
    return the number of files which are not deleted

    when all the files are deleted, then .ppurge dir can be removed
*********************************/
int
rm_purged(struct threadData *cur, char* DirName, time_t purgedir_atime, int purgedir_fd)
{
    DIR *purgeDIR;
    int ret;
    struct dirent *d;
    struct stat f;
    long int fcount =0;
    char *s, *t, *end_dname;

    DEBUG_1("check purgedir: %s\n", DirName);
    if ( (purgeDIR = fdopendir( purgedir_fd )) == NULL ) {
        fprintf( Logfd, "rm_purged - opendir error: %s\n", DirName );
        return -1;
    }
    
    s = cur->dname + strlen(cur->dname);
    *s++ = '/';
    end_dname = s;
    while ( (d = readdir( purgeDIR )) != NULL ) {
        if ( strcmp(".", d->d_name) == 0 ) continue;
        if ( strcmp("..", d->d_name) == 0 ) continue;
        if ( fstatat (purgedir_fd, d->d_name, &f, 0 ) == -1 ) {
            fprintf( Logfd, "fstatat: '%s' %s\n", d->d_name, strerror(errno));
            continue;
        }
        if (purgedir_atime < Ptime && f.st_mtime < Rtime) {
            s = d->d_name; t = end_dname;
            while ( *s )  /* copy file name to end of current path */
                *t++ = *s++;
            *t = '\0';
            if ((ret =unlinkat(purgedir_fd, d->d_name, 0)) != 0) {
                fprintf( Logfd, "rm_purged - unlink failed: '%s' %s\n", d->d_name, strerror(errno));
            } else {
                purgeLog( cur, 'R', &f);
            }
        } else
            fcount +=1;
    }
    DEBUG_1("%s number of files: %ld\n", DirName, fcount);
    closedir(purgeDIR);
    return fcount;
}

/* Open/Create .ppurge directory */
int
create_ppurge(int dirfd, time_t *purgedir_atime)
{
    int purgedir_fd = -1;
    struct stat f;
    int ret;

    /* test if directory exists before creating */
    DEBUG_1("\n");
    if ((purgedir_fd = openat( dirfd, ".ppurge", O_DIRECTORY |O_RDONLY)) != -1)
        if ((ret = fstatat( dirfd, ".ppurge", &f, 0)) != -1 )
            *purgedir_atime = f.st_atime;
        else
            fprintf(Logfd, "create_ppurge - fstatat_1: %s\n", strerror(errno));
    else {
        if ((ret = mkdirat(dirfd, ".ppurge", 01777 )) == 0 ) {
            if ((purgedir_fd = openat( dirfd, ".ppurge", O_DIRECTORY |O_RDONLY)) == -1 )
                fprintf(Logfd, "create_ppurge - openat .ppurge: %s\n", strerror(errno));
            *purgedir_atime = time(NULL);
        } else
            fprintf(Logfd, "create_ppurge - mkdirat .ppurge: %s\n", strerror(errno));
    }
    return purgedir_fd;
}

/* open a log file to record purged files */
void
openLog(time_t now)
{
    char logName[64];

    (void)strftime(logName, 63, "ppurge-%Y.%m.%d-%H_%M_%S.log", localtime(&now));
    if ((Logfd = fopen(logName,  "w")) < 0) {
        fprintf(stderr, "could not open: %s\n", logName);
        exit(errno);
    }
}

int
main( int argc, char* argv[] )
{
    int error, i, pdays = 0;
    int rootfd; 
    time_t now;

    if ( argc < 2 ) {
        printHelp( );
        exit( EXIT_FAILURE );
    }
    now = time(NULL);
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
        if ( !strcmp(*argv, "--exclude" )) {
            argc--; argv++;
            get_exclude_list(*argv, exclude_list);
            verify_paths(exclude_list); }
        if ( !strcmp(*argv, "--purgeDays")) {
            argc--; argv++;
            pdays = atoi(*argv);
            if ( pdays < 1 || pdays > 32000) {
                fprintf(stderr, "purgeDays should be possitive integer between 1 and 32000\n");
                exit(1);
            }
            Ptime = now - (pdays * 86400);
            Rtime = Ptime * 2;
        }
        argc--; argv++;
    }
    openLog(now);
    if (pdays == 0) {
        fprintf(stderr, "--purgeDays must be specified\n");
        exit(1);
    }
    (void) umask((mode_t)00); /* create .ppurge directories with 1777 like /tmp */
    
    fileProcess = &purgeFile;
    for ( i=0; i<MAXTHRDS; i++ ) {
        tdslot[i].THRDid = -1;
        if ( (error = pthread_attr_init( &tdslot[i].tattr )) )
            fprintf( stderr, "Failed to create pthread attr: %s\n", strerror(error));
        else if ( (error = pthread_attr_setdetachstate( &tdslot[i].tattr, PTHREAD_CREATE_DETACHED)) )
            fprintf( stderr, "failed to set attribute detached: %s\n", strerror(error));
    }
    pthread_mutex_init(&mutexFD, NULL);
    pthread_mutex_init(&mutexFileProcess, NULL);

    if ((rootfd = open(*argv, O_DIRECTORY | O_RDONLY)) == -1 ) {
        fprintf( stderr, "Could not open root directory:'%s' %s\n", *argv, strerror(errno));
        exit(errno);
    }
    strcpy( tdslot[0].dname, (const char*) *argv );
    tdslot[0].dirfd = rootfd;
    tdslot[0].THRDid = totalTHRDS++; /* first thread is zero */
    tdslot[0].flag = 0;
    tdslot[0].depth = 0;
    tdslot[0].pstat.st_ino = 0;
    pthread_create( &(tdslot[0].thread_id), &tdslot[0].tattr, fileDir, (void*)&tdslot[0] );
    pthread_exit( NULL );
}
