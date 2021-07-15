struct threadData {
    char dname[FILENAME_MAX+1]; /* full path and basename */
    ino_t pinode;               /* Parent Inode */
    long depth;                 /* directory depth */
    long THRDid;                /* unique ID increaments with each new THRD */
    int  flag;                  /* 0 if thread; recursion > 0 */
    struct stat pstat;          /* Parent inode stat struct */
    pthread_t thread_id;        /* system assigned */
    pthread_attr_t tattr;
    };
