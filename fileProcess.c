/*
 *  fileProcess.c  

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

/* 

For each file found by Pwalk, process the file.
File processing functions go in this file.  File process routines must 
keep the same arguments as defined by the prototype fileProcess()

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include "pwalk.h"

/* conditioanally change file ownership --chown_from --chown_to */
extern uid_t UID_orig, UID_new;
extern gid_t GID_new;
extern int chown_flag;

/* Escape CSV delimeters */
void
csv_escape(char *in, char *out)
{
   char *t, *orig;
   int cnt = 0;

   t = out;
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
       fprintf( stderr, "Bad File: %s\n", orig);
}


/*
 * conditionally change file ownership
 * if file owned by UID_orig chown UID_new:GID_new
 */
void
changeOwner( struct threadData *cur, char *exten, struct stat *f, 
        long fileCnt, /* directory only - count files in directory */
        long dirSz )  /* directory only - sum of files within directory */
{
   int stat;
   char fname[FILENAME_MAX];

   if ( f->st_uid == UID_orig ) {
      csv_escape(cur->dname, fname);
      if (stat = lchown(cur->dname, UID_new, GID_new))
         fprintf(stderr, "could not chown %s\n", fname);
      else {
         fputs(fname, stdout);
         fputc('\n', stdout);
      }
   }
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
