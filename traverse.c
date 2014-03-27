#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>

/*
 * traverse
 * <fn> Full directory path name
 * <pinode> Parent inode
 * <indent> directory Level
 */ 
void 
traverse(char *fn, long pinode, int indent) {
  DIR *dir;
  struct dirent *entry;
  int count;
  char *a, path[1024]; /*** EXTRA STORAGE MAY BE NEEDED ***/
  struct stat info;
  long inode;

  if ( lstat(fn, &info) != 0 )
     fprintf(stderr, "stat() error on %s: %s\n", fn, strerror(errno));
  inode = (long)info.st_ino;
  /* for (count=0; count<indent; count++) printf("  "); */
  printf("%10ld %10ld %d %s\n", (long)info.st_ino, pinode, indent, fn);

  if ((dir = opendir(fn)) == NULL) {
    perror("opendir() error");
    return;
  }
  strcpy(path, fn);
  strcat(path, "/");
  a = &path[0];
  while ( *a ) a++;
  while ((entry = readdir(dir)) != NULL) {
      if ( strcmp(".", entry->d_name) == 0 ) continue;
      if ( strcmp("..",entry->d_name) == 0 ) continue;
        strcpy(a, entry->d_name);
        if ( lstat(path, &info) != 0 )
          fprintf(stderr, "lstat() error on %s: %s\n", path, strerror(errno));
        if (S_ISDIR(info.st_mode))
            traverse(path, inode, indent+1);
        else
            printf("%10ld %10ld %d %s\n", (long)info.st_ino, pinode, indent, path);
  }
  closedir(dir);
}

int
main( int argc, char* argv[] ) {
  if ( argc != 2 ) {
     fputs( "argument required", stderr );
     exit( 1 );
  }
  traverse( argv[1], (long)0, 0);
}
