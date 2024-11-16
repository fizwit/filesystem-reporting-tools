
CC    = gcc
# CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
CFLAGS ?= -O2  
LDFLAGS = -lpthread
DEBUG ?= 0

$(info DEBUG is $(DEBUG))

ifeq ($(DEBUG),1)
    CFLAGS += -DDEBUG=1
endif
ifeq ($(DEBUG),2)
    CFLAGS += -DDEBUG=2
endif 
ifeq ($(DEBUG),3)
    CFLAGS += -DDEBUG=3
endif

default: all

all: pwalk ppurge


pwalk: pwalk.c exclude.c fileProcess.c fileDir.c pwalk.h version.c
	$(CC) $(CFLAGS) -DPWALK -o pwalk version.c exclude.c fileProcess.c fileDir.c pwalk.c $(LDFLAGS) 

ppurge: ppurge.c fileDir.c exclude.c version.c pwalk.h
	$(CC) $(CFLAGS) -Dppurge -o ppurge ppurge.c version.c fileDir.c exclude.c $(LDFLAGS)

