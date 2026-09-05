/*
 *  fileprocess.c

copyright (c) (2013-2016) john f dey

this program is free software; you can redistribute it and/or
modify it under the terms of the gnu general public license
as published by the free software foundation; either version 2
of the license, or (at your option) any later version.

this program is distributed in the hope that it will be useful,
but without any warranty; without even the implied warranty of
merchantability or fitness for a particular purpose.  see the
gnu general public license for more details.

you should have received a copy of the gnu general public license
along with this program; if not, write to the free software
foundation, inc., 51 franklin street, fifth floor, boston, ma  02110-1301, usa.

 */

/*

for each file found by pwalk, process the file.
file processing functions go in this file.  file process routines must
keep the same arguments as defined by the prototype fileprocess()

 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include "pwalk.h"

/* conditioanally change file ownership --chown_from --chown_to */
extern uid_t UID_orig, UID_new;
extern gid_t GID_new;
extern int chown_flag;

/* rewrite control characters */
static const unsigned char escape_code[32] = {
    [7]  = 'a',  // bell
    [8]  = 'b',  // backspace 
    [9]  = 't',  // tab
    [10] = 'n',  // line feed
    [11] = 'v',  // vertical tab
    [12] = 'f',  // form feed
    [13] = 'r'   // carriage return
};

/* Escape CSV delimeters, replace control characters */
void
csv_escape(char *in, char *out)
{
   char *t, *orig;

   t = out;
   orig = in;
   while ( *in ) {
      if ( *in == '"' ) {
          *out++ = '"';
          *out++ = *in++;
      } else if ( (unsigned char)*in < 32 ) {
          if ( escape_code[(int)*in] ) {
              *out++ = '\\';
              *out++ = escape_code[(int)*in];
          }
          in++;
      } else
          *out++ = *in++;
   }
   *out = '\0';
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
      stat = lchown(cur->dname, UID_new, GID_new);
      if (stat == -1)
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
