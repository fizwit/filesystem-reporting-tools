#ifndef REPAIRSHR_H
#define REPAIRSHR_H

#include <sys/types.h>

struct threadData {
    char dname[4096];  // Assuming MAX_PATH is 4096
    ino_t pinode;
    long depth;
};

#endif // REPAIRSHR_H

