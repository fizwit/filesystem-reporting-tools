#define _POSIX_C_SOURCE 200809L

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
void csv_escape(char *in, char *out);

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
      if ((stat = chown((const char*)cur->dname, (uid_t)UID_new, (gid_t)GID_new)))
         fprintf(stderr, "could not chown %s\n", fname);
      else {
         fputs(fname, stdout);
         fputc('\n', stdout);
      }
   }
}
