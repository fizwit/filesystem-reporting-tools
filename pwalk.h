

#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#define MAXEXFILES 512
#define MAXTHRDS 32

#ifdef DEBUG
#if DEBUG == 1
 #define DEBUG_1(fmt, ...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, __VA_ARGS__)
 #define DEBUG_2(fmt, ...) /* Don't do anything for DEBUG level 1 */
#elif DEBUG == 2
 #define DEBUG_1(fmt, ...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, __VA_ARGS__)
 #define DEBUG_2(fmt, ...) fprintf(stderr, "DEBUG: %s(): " fmt, __func__, __VA_ARGS__)
#else
 #define DEBUG_1(fmt, ...) /* Don't do anything in release builds */
 #define DEBUG_2(fmt, ...) /* Don't do anything in release builds */
#endif
#else
 #define DEBUG_1(fmt, ...) /* Don't do anything if DEBUG is not defined */
 #define DEBUG_2(fmt, ...) /* Don't do anything if DEBUG is not defined */
#endif



extern char *exclude_list[MAXEXFILES];

struct threadData {
    char dname[FILENAME_MAX+1]; /* full path and basename */
    ino_t pinode;               /* Parent Inode */
    int dirfd;                  /* file pointer to directory*/
    
    long depth;                 /* directory depth */
    long THRDid;                /* unique ID increaments with each new THRD */
    int  flag;                  /* 0 if thread; recursion > 0 */
    struct stat pstat;          /* Parent inode stat struct */
    pthread_t thread_id;        /* system assigned */
    pthread_attr_t tattr;
    };

extern int ThreadCNT; /* ThreadCNT < MAXTHRDS */
extern int totalTHRDS;
extern struct threadData tdslot[MAXTHRDS];
extern pthread_mutex_t mutexFD;
extern pthread_mutex_t mutexFileProcess;
extern pthread_mutex_t mutexDirProcess;


