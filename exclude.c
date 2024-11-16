#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

void add_exclude_name(char *name)
{
    int i =0;
    size_t len;

    while(exclude_list[i])
        i++;\
    len = strlen(name);
    exclude_list[i] = (char*)malloc(len);
    strcpy(exclude_list[i], name);
    exclude_list[i][len-1] = '\0';
    exclude_list[i+1] = NULL;
}

void
get_exclude_list(char* fname)
{
    FILE *fp;
    char buf[1024];

    fp = fopen(fname, "r");
    if ( fp == NULL ) {
        fprintf(stderr, "could not open: %s\n", fname);
        exit(1);
    }

    while(fgets(buf, 1024, (FILE*) fp))
        add_exclude_name(buf);
    fclose(fp);
}
