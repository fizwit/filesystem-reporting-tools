#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "pwalk.h"

extern int check_exclude_list(char *fname);
/* Process files */
extern void (*fileProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);
extern void (*dirProcess)( struct threadData *cur, char *exten, struct stat *f, long, long);


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
    char *s, *t, *dot, *end_dname;
    int  slot =0;
    DIR *dirp;
    int subfd;
    long localCnt =0; /* number of files in a specific directory */
    long localSz  =0; /* byte cnt of files in the local directory 2010.07 */
    struct dirent *d;
    struct stat f;
    struct threadData *cur, thrd_inst = {.THRDid = -1}, *thrd_ptr = &thrd_inst;
#ifdef PPURGE
    int purge_fcount;
    int purgedir_fd = -1;
    time_t purgedir_atime;
#endif

    cur = (struct threadData *) arg;
    DEBUG_2("threadID=%ld,rdepth=%ld,file=%s\n", cur->THRDid, cur->depth, cur->dname);
    if ((dirp = fdopendir( cur->dirfd )) == NULL ) {
        fprintf( stderr, "Locked Dir: %s\n", cur->dname );
        goto return_thread;
    }
    /* find the end of fs->name and put '/' at the end <end_dname>
       points to char after '/' */
    s = cur->dname + strlen(cur->dname);
    *s++ = '/';
    end_dname = s;
    while ( (d = readdir( dirp )) != NULL ) {
        if ( d->d_name[0] == '.' && 
             (!d->d_name[1] || (d->d_name[1]=='.' && !d->d_name[2]))) continue;
        localCnt++;
        s = d->d_name; t = end_dname;
        while ( *s )  /* copy file name to end of current path <cur->dname> */
            *t++ = *s++;
        *t = '\0';
        if ( fstatat( cur->dirfd, d->d_name, &f, AT_SYMLINK_NOFOLLOW) == -1 ) {
            fprintf( stderr, "threadID=%ld,rdepth=%d fstatat: '%s' %s\n",
              cur->THRDid, cur->flag, strerror(errno), cur->dname);
            continue;
        }
        /* Follow Sub dirs recursivly but don't follow links */
        localSz += f.st_size;
        if ( S_ISDIR(f.st_mode) ) {
#ifdef PPURGE
            if ( !strcmp(".ppurge", d->d_name)) {
                if (purgedir_fd == -1) {
                    purgedir_fd = openat(cur->dirfd, ".ppurge", O_RDONLY);
                    purgedir_atime = f.st_atime;
                }
                continue;
            }
#endif
            if ( exclude_list[0] && check_exclude_list(d->d_name) )
                    continue;
            s = d->d_name; t = end_dname;
            while ( *s )  /* copy file name to end of current path */
                *t++ = *s++;
            if ((subfd = openat(cur->dirfd, d->d_name, O_RDONLY)) == -1 ) {
                fprintf(stderr, "openat fail: %s\n", cur->dname);
                continue;
            }
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
            memcpy( &(thrd_ptr->pstat), &f, sizeof( struct stat ) );  /* <-- what does this do? */
            // strcpy( thrd_ptr->dname, (const char*)cur->dname );
            thrd_ptr->depth  = cur->depth + 1;
            thrd_ptr->pinode = cur->pstat.st_ino; /* Parent Inode */
            if ( thrd_ptr->THRDid != cur->THRDid ) {  /* new thread available */
                DEBUG_1("creating new thread: %s\n", thrd_ptr->dname);
                pthread_create( &tdslot[slot].thread_id, &tdslot[slot].tattr,
                                fileDir, (void*)thrd_ptr );
            } else
                fileDir( (void*) thrd_ptr );
        } else { /* regular file */
            s = end_dname + 1; dot = NULL; /* file extension */
            while ( *s ) {
                if (*s == '.') dot = s+1;
                s++;
            }
#ifdef PPURGE
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
#endif // PPURGE
#ifdef PWALK
           pthread_mutex_lock (&mutexFileProcess);
           (*fileProcess)( cur, dot, &f, (long)-1, (long)0 );
           pthread_mutex_unlock (&mutexFileProcess);
#endif // PWALK
        }
    }
    closedir( dirp );
    *--end_dname = '\0';

#ifdef PWALK
    pthread_mutex_lock (&mutexFileProcess);
    (*fileProcess)( cur, NULL, &cur->pstat, localCnt, localSz);
    pthread_mutex_unlock (&mutexFileProcess);
#endif // PWALK

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