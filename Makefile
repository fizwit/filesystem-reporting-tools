CC    = gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lpthread

default: all

all: pwalk ppurge

pwalk: pwalk.c exclude.c fileProcess.c
	$(CC) $(CFALGS) $(LDFLAGS) -o pwalk exclude.c fileProcess.c pwalk.c

ppurge: ppurge.c 
	$(CC) $(CFLAGS) $(LDFLAGS) -o ppurge ppurge.c

repair-shared: repairshr.c repexcl.c
	$(CC) $(CFALGS) $(LDFLAGS) -o repair-shared repairshr.c repexcl.c

install:
	chown root ppurge
	chmod 4755 ppurge 
