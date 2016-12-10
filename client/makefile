
CC     = gcc
CFLAGS = -g -Wall -pedantic -ansi -pthread -std=c11
LFLAGS = 
#LIBS   = -lsocket  -lnsl -lpthread
LIBS   = -lnsl -lpthread
OBJS   = libnetfiles.o 

all: tester


tester : $(OBJS) tester.c
	$(CC) $(CFLAGS) $(LFLAGS) $(LIBS) -o tester $(OBJS) tester.c


libnetfiles.o: libnetfiles.c libnetfiles.h
	$(CC) $(CFLAGS) -c libnetfiles.c


clean:
	rm -f *.o tester
