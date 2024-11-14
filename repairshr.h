#ifndef REPAIRSHR_H
#define REPAIRSHR_H

#include <sys/types.h>
#include <pthread.h>

#define MAX_PATH 4096

struct threadData {
    char dname[MAX_PATH];
    ino_t pinode;
    int depth;
    long THRDid;
    int flag;
    pthread_t thread_id;
    pthread_attr_t tattr;
};

// Add any function prototypes here
void *repair_directory(void *arg);
// Add other function prototypes as needed
//
// Add any global variable declarations here
extern int MAX_THREADS;
extern struct threadData *tdslot;
extern pthread_mutex_t mutexFD;
extern pthread_mutex_t mutexLog;

#endif // REPAIRSHR_H

