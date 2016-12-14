
CC     = gcc
CFLAGS = -g -Wall -pedantic -ansi -pthread -std=c11
LIBS   = -lnsl -lpthread
OBJS   = libnetfiles.o

all: tester


tester : tester.c
	cp ../server/libnetfiles.o  . 
	cp ../server/libnetfiles.h  . 
	$(CC) $(CFLAGS) $(LIBS) -o tester $(OBJS) tester.c


clean:
	rm -f  tester


