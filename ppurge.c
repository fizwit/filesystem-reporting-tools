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
 - `.ppurge` directories only contain files, no directories are moved.
 - mtime and atime are not affected by move (rename)
 - Remove files from the `.ppurge` cache based on `mtime`.
 - during the cache period users can recover files from `.ppurge`
   by using the "move" command. Do not let users use "copy" cp.
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

A list of pathname with illegal characters are written to the log file
*/

static char *whoami = "ppurge";
static char *Version = "0.1.0 Aug 14 2023 John F Dey john@fuzzdog.com";

/*
 0.1.0  Initial version. Code base copied from pwalk. Purging and reporting
        seems to difficult to perform in one walk of the tree. Purging
        will be a dedicated process.
*/

#define DEBUG 2  // [0, 1, 2]

#if defined(DEBUG) && DEBUG == 1
 #define DEBUG_1(fmt, args...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, ##args)
#elif defined(DEBUG) && DEBUG == 2
 #define DEBUG_1(fmt, args...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, ##args)
 #define DEBUG_2(fmt, args...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, ##args)
#else
 #define DEBUG_1(fmt, args...) /* Don't do anything in release builds */
 #define DEBUG_2(fmt, args...) /* Don't do anything in release builds */
#endif

FILE *Logfd;   /* error log */
time_t Ptime;  /* Purge all files older than this time stamp (less than)*/
time_t Rtime;  /* Remove all files older than this time stamp (Ptime * 2) */
int DEPTH = 0; /* possible furture use for directory purging */

struct threadData {
    char dname[FILENAME_MAX+1]; /* full path and basename */
    int dirfd;                  /* file pointer to directory*/
    long depth;                 /* directory depth */
    long THRDid;                /* unique ID increaments with each new THRD */
    int  flag;                  /* 0 if thread; recursion > 0 */
    pthread_t thread_id;        /* system assigned */
    pthread_attr_t tattr;
};

#define MAXTHRDS 32
int ThreadCNT  = 1; /* ThreadCNT < MAXTHRDS */
int totalTHRDS = 0;
struct threadData tdslot[MAXTHRDS];
pthread_mutex_t mutexFD;

void
printVersion( ) {
   fprintf(stderr, "%s version %s\n", whoami, Version );
   fprintf(stderr, "%s Copyright (C) 2013 John F Dey\n", whoami );
   fprintf(stderr, "ppurge comes with ABSOLUTELY NO WARRANTY;\n" );
   fprintf(stderr, "This is free software, you can redistribute it and/or\n");
   fprintf(stderr, "modify it under the\nterms of the GNU General Public");
   fprintf(stderr, " License as published by the Free Software Foundation;\n");
   fprintf(stderr, "GPL version 3 License\n");
}


void
printHelp()
{
    printf("Useage : %s (fully qualified file name)\n", whoami);
    printf("ppurge should be run daly on volumes with the same value for purgeDays\n");
    printf("Flags: --help\n       --version\n" );
    printf("       --purgeDays (positive integer) Purge files older than n days.\n");
}

/* Escape CSV delimeters */
void
csv_escape(char *in, char *out)
{
   char *orig;
   int cnt = 0;

   orig = in;
   while ( *in ) {
      if ( *in == '"' )
          *out++ = '"';
      if ( (unsigned char)*in < 32 ) {
          in++;
          cnt++;
      } else
          *out++ = *in++;
   *out = '\0';
   }
   if ( cnt )
       fprintf( Logfd, "Bad File Name: %s\n", orig);
}

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

/********************************
    Open a directory and read the conents.
    call opendir with path passed in as an argument
    stat every file from opendir

    If maxthread is not reached creat a new thread and call self
    If no threads available Recursively call self for each directory
    from opendir.

*********************************/
void
*fileDir( void *arg )
{
    char *s, *t, *end_dname;
    int  slot =0, ret;
    int fcount;
    DIR *dirp;
    int subfd, purgedir_fd = -1;
    time_t purgedir_atime;
    long localCnt =0; /* number of files in a specific directory */
    struct dirent *d;
    struct stat f;
    struct threadData *cur, thrd_inst = {.THRDid = -1}, *thrd_ptr = &thrd_inst;

    cur = (struct threadData *) arg;
    DEBUG_2("threadID=%ld,rdepth=%ld,file=%s\n", cur->THRDid, cur->depth, cur->dname);
    if ((dirp = fdopendir( cur->dirfd )) == NULL ) {
        fprintf( Logfd, "Locked Dir: %s\n", cur->dname );
        goto return_thread;
    }
    s = cur->dname + strlen(cur->dname);
    *s++ = '/';
    end_dname = s;
    while ( (d = readdir( dirp )) != NULL ) {
        if ( d->d_name[0] == '.' && 
             (!d->d_name[1] || (d->d_name[1]=='.' && !d->d_name[2]))) continue;
        s = d->d_name; t = end_dname;
        while ( *s )  /* copy file name to end of cur->dname */
            *t++ = *s++;
        *t = '\0';
        if ( fstatat( cur->dirfd, d->d_name, &f, AT_SYMLINK_NOFOLLOW) == -1 ) {
            fprintf( Logfd, "threadID=%ld,rdepth=%d fstatat: '%s' %s\n",
              cur->THRDid, cur->flag, strerror(errno), cur->dname);
            continue;
        }
        fprintf(stderr, "%8ld %s\n",f.st_size, cur->dname);
        /* Follow Sub dirs recursivly but don't follow links */
        if ( S_ISDIR(f.st_mode) ) {
            if ( !strcmp(".ppurge", d->d_name)) {
                if (purgedir_fd == -1) {
                    purgedir_fd = openat(cur->dirfd, ".ppurge", O_RDONLY);
                    purgedir_atime = f.st_atime;
                }
                continue;
            }
            s = d->d_name; t = end_dname;
            while ( *s )  /* copy file name to end of current path */
                *t++ = *s++;
            if ((subfd = openat(cur->dirfd, d->d_name, O_RDONLY)) == -1 ) {
                fprintf(Logfd, "openat fail: %s\n", cur->dname);
                continue;
            }
            DEBUG_1("follow directory: %s\n", cur->dname);
            pthread_mutex_lock (&mutexFD);
            if ( ThreadCNT < MAXTHRDS ) {
                slot = 0;
                while ( slot < MAXTHRDS ) {
                    if ( tdslot[slot].THRDid == -1 ) {
                        thrd_ptr = &tdslot[slot];
                        thrd_ptr->THRDid = totalTHRDS++;
                        thrd_ptr->flag = 0;   /* recurse flag reset for thread instance */
                        thrd_ptr->dirfd = subfd;
                        break;
                    }
                    slot++;
                }
                ThreadCNT++; /* allocate the thread */
            } else {
                thrd_ptr = &thrd_inst;
                thrd_ptr->THRDid = cur->THRDid;
                thrd_ptr->flag = cur->flag + 1;
                thrd_ptr->dirfd = subfd;
            }
            pthread_mutex_unlock (&mutexFD);
            /* create ponter to tdslot that will be used for next
               call to fileDir - local or from array - shorten next block of
               code
             */
            strcpy( thrd_ptr->dname, (const char*)cur->dname );
            thrd_ptr->depth  = cur->depth + 1;
            if ( thrd_ptr->THRDid != cur->THRDid ) {  /* new thread available */
                DEBUG_1("creating new thread: %s\n", thrd_ptr->dname);
                pthread_create( &tdslot[slot].thread_id, &tdslot[slot].tattr,
                                fileDir, (void*)thrd_ptr );
            } else
                fileDir( (void*) thrd_ptr );
        } else { /* regular file */
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
                localCnt++;
        }
    }
    if ( purgedir_fd != -1 ) {
        fcount = rm_purged(cur, cur->dname, purgedir_atime, purgedir_fd);
        if ( fcount == 0 )  /* directory is empty */
            if ((ret = unlinkat(cur->dirfd, ".ppurge", AT_REMOVEDIR)) != 0) {
                fprintf( Logfd, "unlink .ppurge failed: '%s'\n", strerror(errno));
            }

    }
    closedir( dirp );
    *--end_dname = '\0';

return_thread:
    if ( cur->flag == 0 ) { /* this instance of fileDir is a thread */
        pthread_mutex_lock ( &mutexFD );
        DEBUG_2("msg=endTHRD,threadID=%ld,rdepth=%d,file=<%s>\n", cur->THRDid, cur->flag, cur->dname);
        --ThreadCNT;
        cur->THRDid = -1;
        pthread_mutex_unlock ( &mutexFD );
        pthread_exit( EXIT_SUCCESS );
    } else
        return 0;
}

/* open a log file */
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
    argc--; argv++;
    now = time(NULL);
    while ( argc > 0 && *argv[0] == '-' ) {
        if ( !strcmp(*argv, "--depth" ) ) {
           argc--; argv++;
           DEPTH = atoi(*argv);
        }
        if ( !strcmp(*argv, "--help" ) ) {
           printHelp( );
           exit(0); }
        if ( !strcmp(*argv, "--version" ) || !strcmp(*argv, "-v") ) {
           printVersion( );
           exit(0); }
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
    if ( setuid((uid_t) 0)) {
       fprintf(stderr, "unable to setuid root; not all files will be processed\n");
       exit(1);
    }
    (void) umask((mode_t)00); /* create .ppurge directories with 1777 like /tmp */
    

    for ( i=0; i<MAXTHRDS; i++ ) {
        tdslot[i].THRDid = -1;
        if ( (error = pthread_attr_init( &tdslot[i].tattr )) )
            fprintf( Logfd, "Failed to create pthread attr: %s\n", strerror(error));
        else if ( (error = pthread_attr_setdetachstate( &tdslot[i].tattr, PTHREAD_CREATE_DETACHED)) )
            fprintf( Logfd, "failed to set attribute detached: %s\n", strerror(error));
    }
    pthread_mutex_init(&mutexFD, NULL);

    if ((rootfd = open(*argv, O_DIRECTORY | O_RDONLY)) == -1 ) {
        fprintf( stderr, "Could not open root directory:'%s' %s\n", *argv, strerror(errno));
        exit(errno);
    }
    strcpy( tdslot[0].dname, (const char*) *argv );
    tdslot[0].dirfd = rootfd;

    tdslot[0].THRDid = totalTHRDS++; /* first thread is zero */
    tdslot[0].flag = 0;
    tdslot[0].depth = 0;
    pthread_create( &(tdslot[0].thread_id), &tdslot[0].tattr, fileDir, (void*)&tdslot[0] );
    pthread_exit( NULL );
}
