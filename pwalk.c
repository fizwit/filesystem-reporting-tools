/*
 *  pwalk.c  Parrallel Walk a file system and report file meta data 

Copyright (C) 2013 John F Dey 

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

 *  pwalk is inspired by du but designed to be used with large 
 *  file systems ( > 10 million files ) 
 *  
 *  History: dir.c, walk.c, walkv2,3,4, pwalkfs.c
 *
 *  Example of using the directory call, opendir and readdir to
 *  simulate ls.
 *
 *  1997.03.20 John Dey Although this is the first documented date for
 *             this file I have versions that date from 1988. 
 *  2002.09.04 John Dey walk the directory and gather stats
 *  2002.09.06 John Dey make to look like du -a
 *  2004.07.06 John Dey add -a and -k 
 *  2008.04.01 John Dey CSV output for database use
 *  2009.04.12 John Dey v3.1
 *  replaced constants with "FILENAME_MAX", 
 *  Size of directory is size of all files in directory plus itself
 *  Added printStat function
 *  print file count on line with direcories
 *  2009.05.18 check for control charaters and double qutoes in file names; 
 *  escape the double quotes and print bad file names to stderr
 *  2009.12.30 size for dir should just be dir size; Fix; count returns 0 
 *  for normal files and count of just the local directory; Previously count
 *  return the recursive file count for the whole tree. 
 *
    2010.01.08 john dey; New field to output: file name extension. 
    Extension is defined as the last part of the name after a Dot "." 
    if no dot is found extension is empty ""
    new feature: accepts multible dirctory names as cmd line argument

This line of code has been replaced 
     if ( f.st_mode & S_IFDIR && (f.st_mode & S_IFMT != S_IFLNK) ) {
With this new line of code:
     if ( S_ISDIR(f.st_mode)  ) { Or I could have done: if ( (f.st_mode & S_IFDIR) == S_IFDIR ) 
   2010.01.11  John Dey
   Complete re-write of walkv4 transforming it into pwalk.
   pwalk is a threaded version of walkv4.
   pwalk will call fileDir as a new thread until MAXTHRDS is reached.
   2010.02.01 pwalk v1 did not detach nor did it join the theads; v2
   fixes this short comming;

   2010.03.24 john dey; New physical hardware is available to run pwalk.
   16 threads are only using about 20% CPU with 10% IO wait. Based on this
   the thread count will be doubled to 32.
   2010.11.29 Add mutex for printStat    
   2012.10.09 --NoSnap command line arrgument added.  
              ignore directories that have the name .snapshot
   2013.08.02 john f dey; Add GNU license, --version command line arg added
   2013.10.07 john f dey; 
      new Feature: Keep track of directory level, output parent inode
      Each file has its inode and parent inode.  This will allow a topological view 
      of the file system.
      The top level dir has a parnet inode of 0. So finding the top of tree has been
      made easy.
      Changes:  value of "fileCnt" defaults to -1 if file is not a directory
      many directories have zero files, zero is valid file count for a directory
      This change will effect many SQL queries. Earlier versions were incorrent to
      asume fileCNT == 0 is a regular file. Extra quotes have been removed from the 
      CSV file.  This will save a small amount of file space for the output files.
      (a few megabytes for some of our larger directories)
      Improvements:  data structures are cleaned up. changes made to improve thread
      performance. Expecthing this version to be 10% to 15% faster than previous.
      

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

#undef THRD_DEBUG

static char *Version = "2.6 Nov 15 2013 John F Dey john@fuzzdog.com";
static char *whoami = "pwalk";

int SNAPSHOT =0; /* if set ignore directories called .snapshot */

struct threadData {
    char dname[FILENAME_MAX+1]; /* full path and basename */
    long pinode;                /* Parent Inode */
    long depth;                 /* directory depth */
    long THRDid;                /* unique ID increaments with each new THRD */
    int  flag;                  /* 0 if thread; recursion > 0 */
    struct stat pstat;          /* Parent inode stat struct */
    pthread_t thread_id;        /* system assigned */
    pthread_attr_t tattr;
    };

#define MAXTHRDS 32
int ThreadCNT  = 1; /* ThreadCNT < MAXTHRDS */
int totalTHRDS =0;
struct threadData tdslot[MAXTHRDS];
pthread_mutex_t mutexFD;
pthread_mutex_t mutexPrintStat;

void
printVersion( ) {
   fprintf( stderr, "%s version %s\n", whoami, Version );
   fprintf( stderr, "%s Copyright (C) 2013 John F Dey\n", whoami ); 
   fprintf( stderr, "pwalk comes with ABSOLUTELY NO WARRANTY;\n" );
   fprintf( stderr, "This is free software, you can redistribute it and/or\n");
   fprintf( stderr, "modify it under the terms of the GNU General Public License\n" );
   fprintf( stderr, "as published by the Free Software Foundation; either version 2\n" );
   fprintf( stderr, "of the License, or (at your option) any later version.\n" );
}

void
printHelp( ) {
   fprintf( stderr, "Useage : %s (fully qualified file name)\n", whoami); 
   fprintf( stderr, "Flags: --help --version \n" );
   fprintf( stderr, "       --NoSnap Ignore directories with name .snapshot\n");
   fprintf( stderr, "output format: CSV\n" );
   fprintf( stderr, "fields : inode,parent-inode,directory-depth,\"filename\",\"fileExtension\",UID,GID,st_size,st_blocks\"," );
   fprintf( stderr, "\"st_mode\",atime,mtime,ctime,count(files),sum(size)\n");
}

/*
 *  this needs to be in a crital secion  (and it is!)
 */
void
printStat( struct threadData *cur, char *exten, struct stat *f, 
        long fileCnt, /* directory only - count files in directory */
        long dirSz )  /* directory only - sum of files within directory */
{
   char new[FILENAME_MAX+FILENAME_MAX];
   char out[FILENAME_MAX+FILENAME_MAX];
   char *s, *t = new;
   int cnt = 0;
   long ino, pino, depth;
   char Sep=',';  /* this was added to help with debugging */

   cnt =0;
   /* fix bad file name is moved inside printStat to make it thread safe */
   s = cur->dname;
   while ( *s ) {
      if ( *s == '"' )
         *t++ = '\\';
      if ( *s < 32 ) {
         s++;
         cnt++;
      } else
         *t++ = *s++;
   }
   *t++ = *s++;
   if ( cnt )
      fprintf( stderr, "Bad File: %s\n", new );
   if ( fileCnt != -1 ) {  /* directory */
      ino = f->st_ino; pino = cur->pinode; depth = cur->depth - 1;}
   else {  /* Not a directory */
      ino = f->st_ino; pino = cur->pstat.st_ino; depth = cur->depth; }
   sprintf ( out, "%ld,%ld,%ld,\"%s\",\"%s\",%ld,%ld,%ld,%ld,\"%07o\",%ld,%ld,%ld,%ld,%ld\n",
    ino, pino, depth,
    new, (exten)? exten:"", (long)f->st_uid,
    (long)f->st_gid, (long)f->st_size, (long)f->st_blocks, (int)f->st_mode,
    (long)f->st_atime, (long)f->st_mtime, (long)f->st_ctime, 
    fileCnt, dirSz );
    fputs( out, stdout );
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
    int  slot, status;
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
        exit ( 1 );
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
            fprintf( stderr, "msg=lstat error,threadID=%ld,rdepth=%d,file=%s\n", 
              cur->THRDid, cur->flag, cur->dname );
            continue;
        } 
        /* Follow Sub dirs recursivly but don't follow links */
        localSz += f.st_size;
        if ( S_ISDIR(f.st_mode) ) {
            if ( SNAPSHOT && !strcmp( ".snapshot", d->d_name ) ) {
               continue; /* next file from raddir */
            }
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
           s = end_dname + 1; dot = '\0'; /* file extension */
           while ( *s ) { 
               if (*s == '.') dot = s+1; 
                    s++; }
           pthread_mutex_lock (&mutexPrintStat);
           printStat( cur, dot, &f, (long)-1, (long)0 );
           pthread_mutex_unlock (&mutexPrintStat);
        }
    }
    closedir( dirp );
    *--end_dname = '\0';
    s = end_dname - 1; dot = '\0';
    while ( *s != '/' ) { if (*s == '.') dot = s+1; s--; }
    if ( s+2 == dot ) /* this dot is next to the slash like /.R */
        dot = '\0';
    pthread_mutex_lock (&mutexPrintStat);
    printStat( cur, dot, &cur->pstat, localCnt, localSz );
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
    int error, i;
    char *s, *c;
    struct stat f;

    if ( argc < 2 ) {
        printHelp( ); 
        exit( EXIT_FAILURE );
    }
    argc--; argv++; 
    while ( argc > 0 && *argv[0] == '-' ) {
        if ( !strcmp( *argv, "--NoSnap" ) )
           SNAPSHOT = 1; 
        if ( !strcmp( *argv, "--help" ) )
           printHelp( );
        if ( !strcmp( *argv, "--version" ) || !strcmp( *argv, "-v" ) ) 
           printVersion( );
        argc--; argv++;
    }
    if ( argc == 0 ) exit ( 0 );
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
    if ( lstat( *argv, &f ) == -1 ) {
        perror( "lstat: " ); 
        exit( 1 ); 
    }
    memcpy( &tdslot[0].pstat, &f, sizeof( struct stat ) );
    tdslot[0].THRDid = totalTHRDS++; /* first thread is zero */
    tdslot[0].flag = 0;
    tdslot[0].depth = 0;
    tdslot[0].pinode = 0;
    pthread_create( &(tdslot[0].thread_id), &tdslot[0].tattr, fileDir, 
                    (void*)&tdslot[0] );
    pthread_exit( NULL );
}
