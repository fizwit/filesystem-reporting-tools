CC      = gcc
CFLAGS  = -O2 -Wall -pthread
LDFLAGS = -lpthread

default: all

all: pwalk ppurge

pwalk: pwalk.c exclude.c fileProcess.c
	$(CC) $(CFLAGS) -o pwalk exclude.c fileProcess.c pwalk.c $(LDFLAGS)

ppurge: ppurge.c
	$(CC) $(CFLAGS) -o ppurge ppurge.c $(LDFLAGS)

repair-shared: repairshr.c repexcl.c
	$(CC) $(CFLAGS) -o repair-shared repairshr.c repexcl.c $(LDFLAGS)

install:
	chown root ppurge
	chmod 4755 ppurge

