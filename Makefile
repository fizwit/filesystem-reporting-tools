
CC    = gcc
# CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99
CFLAGS ?= -O2  
LDFLAGS = -lpthread
PW_MAXTHRDS ?= 32
PW_DEBUG ?= 0

CFLAGS += -DPW_MAXTHRDS=$(PW_MAXTHRDS)
$(info PW_DEBUG is $(PW_DEBUG))

ifeq ($(PW_DEBUG),1)
    CFLAGS += -DPW_DEBUG=1
endif
ifeq ($(PW_DEBUG),2)
    CFLAGS += -DPW_DEBUG=2
endif 
ifeq ($(PW_DEBUG),3)
    CFLAGS += -DPW_DEBUG=3
endif

default: all

all: pwalk ppurge


pwalk:   pwalk.c exclude.c fileProcess.c changeOwn.c version.c pwalk.h
	$(CC) $(CFLAGS) -DPWALK -o pwalk pwalk.c version.c exclude.c fileProcess.c changeOwn.c $(LDFLAGS) 

ppurge: ppurge.c version.c pwalk.h
	$(CC) $(CFLAGS) -Dppurge -o ppurge ppurge.c version.c fileProcess.c $(LDFLAGS)

