#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "repairshr.h"

extern char *exclude_list[512];

int check_exclude_list(char *fname) {
    int i = 0;
    while(exclude_list[i]) {
        if (strcmp(exclude_list[i], fname) == 0) {
            return 1;
        }
        i++;
    }
    return 0;
}

void verify_paths(char *list[]) {
    // This function can be implemented to verify the paths in the exclude list
    // For now, we'll leave it empty
}

void get_exclude_list(char* fname, char *list[]) {
    FILE *fp = fopen(fname, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error opening exclude list file: %s\n", fname);
        return;
    }

    char line[256];
    int i = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline
        list[i] = strdup(line);
        i++;
        if (i >= 512) break;  // Prevent buffer overflow
    }
    list[i] = NULL;  // Null-terminate the list

    fclose(fp);
}
