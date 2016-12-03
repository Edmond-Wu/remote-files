CC = gcc
CFLAGS = -g -Wall -pedantic -ansi -std=c11
OBJS = libnetfiles.o
OBJS2 = netfileserver.o
DEPS = libnetfiles.h

all: libnetfiles netfileserver
	rm *.o

libnetfiles : $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o libnetfiles

netfileserver : $(OBJS2)
	$(CC) $(CFLAGS) $(OBJS2) -o netfileserver

libnetfiles.o: libnetfiles.c libnetfiles.h
	$(CC) $(CFLAGS) -c libnetfiles.c

netfileserver.o: netfileserver.c
	$(CC) $(CFLAGS) -c netfileserver.c

clean:
	rm memgrind
