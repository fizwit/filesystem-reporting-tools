/*
 *  repair-shared. Parallel permission repair for shared folders
Copyright (C) (2024) John F Dey
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <grp.h>
#include <stdarg.h>
#include "repairshr.h"

#define MAX_PATH 4096
#define MAXEXFILES 512
#define MAX_GROUPS 100

int SNAPSHOT = 0;
int ONE_FS = 0;
int DRY_RUN = 0;
dev_t ST_DEV;

char *exclude_list[MAXEXFILES];
gid_t change_groups[MAX_GROUPS];
int change_groups_count = 0;

pthread_mutex_t mutexFD;
pthread_mutex_t mutexLog;

// Function declarations
int check_exclude_list(char *fname);
void get_exclude_list(char* fname, char *list[]);
void verify_paths(char *list[]);

void log_change(const char *format, ...) {
    va_list args;
    va_start(args, format);
    pthread_mutex_lock(&mutexLog);
    vprintf(format, args);
    pthread_mutex_unlock(&mutexLog);
    va_end(args);
}

void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    pthread_mutex_lock(&mutexLog);
    vfprintf(stderr, format, args);
    pthread_mutex_unlock(&mutexLog);
    va_end(args);
}

int should_change_group(gid_t gid) {
    int i;
    for (i = 0; i < change_groups_count; i++) {
        if (gid == change_groups[i]) {
            return 1;
        }
    }
    return 0;
}

gid_t find_non_private_group(const char *path, gid_t start_gid) {
    char current_path[MAX_PATH];
    struct stat st;
    strncpy(current_path, path, sizeof(current_path));

    while (strlen(current_path) > 1) {
        if (lstat(current_path, &st) == 0) {
            if (st.st_gid != st.st_uid && st.st_gid != 0 && !should_change_group(st.st_gid)) {
                return st.st_gid;
            }
        }
        char *last_slash = strrchr(current_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
        } else {
            break;
        }
    }

    // If we reach here, we didn't find a suitable non-private, non-root group
    return 0;  // Indicate that no suitable group was found
}

void repair_permissions(const char *path, struct stat *st) {
    mode_t new_mode = st->st_mode;
    gid_t new_gid = st->st_gid;
    int changes = 0;

    // Set setgid bit on directories
    if (S_ISDIR(st->st_mode) && !(st->st_mode & S_ISGID)) {
        new_mode |= S_ISGID;
        changes = 1;
    }

    // Check for private group, root group, or groups to change
    if (st->st_gid == st->st_uid || st->st_gid == 0 || should_change_group(st->st_gid)) {
        gid_t non_private_gid = find_non_private_group(path, st->st_gid);
        if (non_private_gid != 0) {
            new_gid = non_private_gid;
            changes = 1;
        } else {
            log_error("Error: No suitable non-private, non-root group found for %s (current gid: %d)\n", path, st->st_gid);
        }
    }

    // Ensure minimum group permissions
    if (S_ISDIR(st->st_mode)) {
        if ((st->st_mode & S_IRGRP) == 0 || (st->st_mode & S_IXGRP) == 0) {
            new_mode |= S_IRGRP | S_IXGRP;
            changes = 1;
        }
    } else {
        if ((st->st_mode & S_IRGRP) == 0) {
            new_mode |= S_IRGRP;
            changes = 1;
        }
    }

    // Apply changes if needed
    if (changes) {
        if (new_mode != st->st_mode) {
            if (DRY_RUN) {
                log_change("Would change mode of %s from %o to %o\n", path, st->st_mode, new_mode);
            } else {
                if (chmod(path, new_mode) != 0) {
                    log_error("Error: Failed to change mode for %s: %s\n", path, strerror(errno));
                } else {
                    log_change("Changed mode of %s from %o to %o\n", path, st->st_mode, new_mode);
                }
            }
        }

        if (new_gid != st->st_gid) {
            if (DRY_RUN) {
                log_change("Would change group of %s from %d to %d\n", path, st->st_gid, new_gid);
            } else {
                if (chown(path, -1, new_gid) != 0) {
                    log_error("Error: Failed to change group for %s: %s\n", path, strerror(errno));
                } else {
                    log_change("Changed group of %s from %d to %d\n", path, st->st_gid, new_gid);
                }
            }
        }
    }
}

void *repair_directory(void *arg) {
    struct threadData *cur = (struct threadData *)arg;
    DIR *dirp;
    struct dirent *d;
    char path[MAX_PATH];
    struct stat st;

    if ((dirp = opendir(cur->dname)) == NULL) {
        log_error("Error: Unable to open directory %s: %s\n", cur->dname, strerror(errno));
        return NULL;
    }

    while ((d = readdir(dirp)) != NULL) {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", cur->dname, d->d_name);

        if (lstat(path, &st) == -1) {
            log_error("Error: Unable to stat %s: %s\n", path, strerror(errno));
            continue;
        }

        if (ONE_FS && st.st_dev != ST_DEV) {
            continue;
        }

        repair_permissions(path, &st);

        if (S_ISDIR(st.st_mode)) {
            if (SNAPSHOT && strcmp(d->d_name, ".snapshot") == 0) {
                continue;
            }

            if (check_exclude_list(path)) {
                continue;
            }

            struct threadData new_td;
            strncpy(new_td.dname, path, sizeof(new_td.dname));
            new_td.pinode = st.st_ino;
            new_td.depth = cur->depth + 1;

            repair_directory(&new_td);
        }
    }

    closedir(dirp);
    return NULL;
}

void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [options] <directory>\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --NoSnap            Ignore .snapshot directories\n");
        fprintf(stderr, "  --exclude <file>    Specify a file containing paths to exclude\n");
        fprintf(stderr, "  -x, --one-file-system  Stay on one file system\n");
        fprintf(stderr, "  --dry-run           Show changes without making them\n");
        fprintf(stderr, "  --change-gids <gids>  Comma-separated list of group IDs to change\n");
        exit(1);
    }

    // Parse command-line arguments
    int i;
    char *directory = NULL;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--NoSnap") == 0) {
                SNAPSHOT = 1;
            } else if (strcmp(argv[i], "--exclude") == 0) {
                if (++i < argc) {
                    get_exclude_list(argv[i], exclude_list);
                    verify_paths(exclude_list);
                } else {
                    fprintf(stderr, "Error: --exclude requires a filename\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--one-file-system") == 0) {
                ONE_FS = 1;
            } else if (strcmp(argv[i], "--dry-run") == 0) {
                DRY_RUN = 1;
            } else if (strcmp(argv[i], "--change-gids") == 0) {
                if (++i < argc) {
                    char *token = strtok(argv[i], ",");
                    while (token != NULL && change_groups_count < MAX_GROUPS) {
                        change_groups[change_groups_count++] = (gid_t)atoi(token);
                        token = strtok(NULL, ",");
                    }
                } else {
                    fprintf(stderr, "Error: --change-gids requires a comma-separated list of group IDs\n");
                    exit(1);
                }
            } else if (argv[i][1] == '-') {
                fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                exit(1);
            } else if (strlen(argv[i]) == 2) {
                fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                exit(1);
            } else {
                fprintf(stderr, "Error: Invalid option '%s'\n", argv[i]);
                exit(1);
            }
        } else if (directory == NULL) {
            directory = argv[i];
        } else {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[i]);
            exit(1);
        }
    }

    if (directory == NULL) {
        fprintf(stderr, "Error: No directory specified\n");
        exit(1);
    }

    // Remove trailing slash from directory path
    remove_trailing_slash(directory);

    if (DRY_RUN) {
        printf("Dry run mode: No changes will be made to the file system\n");
    }

    struct stat root_st;
    if (lstat(directory, &root_st) == -1) {
        fprintf(stderr, "Error: Unable to stat root directory %s: %s\n", directory, strerror(errno));
        exit(1);
    }

    ST_DEV = root_st.st_dev;

    pthread_mutex_init(&mutexFD, NULL);
    pthread_mutex_init(&mutexLog, NULL);

    struct threadData root_td;
    strncpy(root_td.dname, directory, sizeof(root_td.dname));
    root_td.pinode = 0;
    root_td.depth = 0;

    repair_directory(&root_td);

    pthread_mutex_destroy(&mutexFD);
    pthread_mutex_destroy(&mutexLog);

    return 0;
}