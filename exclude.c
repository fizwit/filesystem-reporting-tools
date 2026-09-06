#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>

extern char *exclude_list[512];

int
check_exclude_list(char *fname)
{
    int i =0;
    while(exclude_list[i])
       if ( !strcmp(exclude_list[i++], fname))
           return 1;
    return 0;
}

void
verify_paths(char *list[])
{
    int i=0;
    struct stat f;

    while(list[i]) {
        if ( lstat( list[i], &f ) == -1 )
            fprintf(stderr, "verify not found: %s", list[i]);
        i++;
    }
}

void
get_exclude_list(char* fname, char *list[])
{
    FILE *fp;
    char *a, buf[1024];
    size_t len;
    int i =0;

    fp = fopen(fname, "r");
    if ( fp == NULL ) {
        fprintf(stderr, "could not open: %s\n", fname);
        exit(1);
    }

    while(fgets(buf, 1024, (FILE*) fp)) {
        len = strlen(buf);
        buf[len-1] = '\0';
        a = (char*)malloc(len);
        list[i++] = strncpy(a, buf, len);
    }
    list[i] = NULL;
    fclose(fp);
}
