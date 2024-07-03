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

#define VERSION "0.5"
#define MAX_PATH 4096
#define MAXEXFILES 512
#define MAX_GROUPS 100
#define MAX_EXCLUDES 100

int MAX_THREADS = 32;
int SNAPSHOT = 0;
int ONE_FS = 0;
int DRY_RUN = 0;
int FORCE_GROUP_WRITABLE = 0;
dev_t ST_DEV;

char *exclude_list[MAXEXFILES];
gid_t change_groups[MAX_GROUPS];
int change_groups_count = 0;

int ThreadCNT = 1; /* ThreadCNT < MAX_THREADS */
int totalTHRDS = 0;
struct threadData *tdslot;

pthread_mutex_t mutexFD;
pthread_mutex_t mutexLog;

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
            log_error("Error: No suitable non-private, non-root group found for %s (current gid: %d, uid: %d)\n", 
                      path, st->st_gid, st->st_uid);
        }
    }

    // Ensure minimum group permissions
    if (S_ISDIR(st->st_mode)) {
        if (FORCE_GROUP_WRITABLE) {
            if ((st->st_mode & S_IRWXG) != (S_IRWXG)) {
                new_mode |= S_IRWXG;  // Read, write, and execute for group
                changes = 1;
            }
        } else {
            if ((st->st_mode & S_IRGRP) == 0 || (st->st_mode & S_IXGRP) == 0) {
                new_mode |= S_IRGRP | S_IXGRP;
                changes = 1;
            }
        }
    } else {
        if (FORCE_GROUP_WRITABLE) {
            if ((st->st_mode & (S_IRGRP | S_IWGRP)) != (S_IRGRP | S_IWGRP)) {
                new_mode |= S_IRGRP | S_IWGRP;  // Read and write for group
                changes = 1;
            }
        } else {
            if ((st->st_mode & S_IRGRP) == 0) {
                new_mode |= S_IRGRP;
                changes = 1;
            }
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

            int i;
            pthread_mutex_lock(&mutexFD);
            int slot = -1;
            if (ThreadCNT < MAX_THREADS) {
                for (i = 0; i < MAX_THREADS; i++) {
                    if (tdslot[i].THRDid == -1) {
                        slot = i;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&mutexFD);

            struct threadData *new_td;
            if (slot != -1) {
                new_td = &tdslot[slot];
                new_td->THRDid = totalTHRDS++;
                new_td->flag = 0;
                ThreadCNT++;
            } else {
                new_td = malloc(sizeof(struct threadData));
                new_td->THRDid = cur->THRDid;
                new_td->flag = cur->flag + 1;
            }

            strncpy(new_td->dname, path, sizeof(new_td->dname));
            new_td->pinode = st.st_ino;
            new_td->depth = cur->depth + 1;

            if (slot != -1) {
                pthread_create(&new_td->thread_id, &new_td->tattr, repair_directory, (void *)new_td);
            } else {
                repair_directory((void *)new_td);
                free(new_td);
            }
        }
    }

    closedir(dirp);

    if (cur->flag == 0) {
        pthread_mutex_lock(&mutexFD);
        ThreadCNT--;
        cur->THRDid = -1;
        pthread_mutex_unlock(&mutexFD);
        pthread_exit(NULL);
    }

    return NULL;
}

void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

void print_help(const char *program_name) {
    fprintf(stderr, "Usage: %s [options] <folder>\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --dry-run                Show changes without making them\n");    
    fprintf(stderr, "  --NoSnap                 Ignore .snapshot directories\n");
    fprintf(stderr, "  --exclude <path>         Specify a full path to exclude (can be used multiple times)\n");
    fprintf(stderr, "  --change-gids <gids>     Comma-separated list of group IDs to change to next group up\n");
    fprintf(stderr, "  --force-group-writable   Make all files and folders group readable and writable\n");
    fprintf(stderr, "  --threads <num>          Set maximum number of threads (current default: %d)\n", MAX_THREADS);        
    fprintf(stderr, "  -x, --one-file-system    Stay on one file system\n");
    fprintf(stderr, "  --version                Display version information and exit\n");
    fprintf(stderr, "  --help                   Display this help message and exit\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        exit(1);
    }

    // Parse command-line arguments
    int i;
    char *directory = NULL;
    char *exclude_files[MAX_EXCLUDES] = {NULL};
    int exclude_count = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--NoSnap") == 0) {
                SNAPSHOT = 1;
            } else if (strcmp(argv[i], "--exclude") == 0) {
                if (++i < argc) {
                    if (exclude_count < MAX_EXCLUDES) {
                        exclude_files[exclude_count++] = argv[i];
                    } else {
                        fprintf(stderr, "Error: Too many exclude paths specified. Maximum is %d.\n", MAX_EXCLUDES);
                        exit(1);
                    }
                } else {
                    fprintf(stderr, "Error: --exclude requires a path\n");
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
            } else if (strcmp(argv[i], "--force-group-writable") == 0) {
                FORCE_GROUP_WRITABLE = 1;
            } else if (strcmp(argv[i], "--threads") == 0) {
                if (++i < argc) {
                    MAX_THREADS = atoi(argv[i]);
                    if (MAX_THREADS <= 0) {
                        fprintf(stderr, "Error: Invalid thread count. Must be a positive integer.\n");
                        exit(1);
                    }
                } else {
                    fprintf(stderr, "Error: --threads requires a number\n");
                    exit(1);
                }                
            } else if (strcmp(argv[i], "--version") == 0) {
                printf("%s version %s\n", argv[0], VERSION);
                exit(0);
            } else if (strcmp(argv[i], "--help") == 0) {
                print_help(argv[0]);
                exit(0);
            } else if (argv[i][1] == '-') {
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
        print_help(argv[0]);
        exit(1);
    }

    // Process all exclude files
    for (i = 0; i < exclude_count; i++) {
        get_exclude_list(exclude_files[i], exclude_list);
    }
    verify_paths(exclude_list);

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

    tdslot = malloc(sizeof(struct threadData) * MAX_THREADS);
    if (tdslot == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for thread data\n");
        exit(1);
    }
    pthread_mutex_init(&mutexFD, NULL);
    pthread_mutex_init(&mutexLog, NULL);

    // Initialize thread attributes
    int error;
    for (i = 0; i < MAX_THREADS; i++) {
        tdslot[i].THRDid = -1;
        if ((error = pthread_attr_init(&tdslot[i].tattr))) {
            fprintf(stderr, "Failed to create pthread attr: %s\n", strerror(error));
        } else if ((error = pthread_attr_setdetachstate(&tdslot[i].tattr, PTHREAD_CREATE_DETACHED))) {
            fprintf(stderr, "Failed to set attribute detached: %s\n", strerror(error));
        }
    }

    struct threadData root_td;
    strncpy(root_td.dname, directory, sizeof(root_td.dname));
    root_td.pinode = 0;
    root_td.depth = 0;
    root_td.THRDid = totalTHRDS++;
    root_td.flag = 0;

    pthread_create(&root_td.thread_id, &tdslot[0].tattr, repair_directory, (void *)&root_td);

    // Wait for all threads to complete
    while (ThreadCNT > 0) {
        usleep(1000);
    }

    pthread_mutex_destroy(&mutexFD);
    pthread_mutex_destroy(&mutexLog);
    free(tdslot);

    return 0;
}
