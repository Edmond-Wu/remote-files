
CC     = gcc
CFLAGS = -g -Wall -pedantic -ansi -pthread -std=c11
#LFLAGS = -L/usr/lib 
LFLAGS =  
LIBS   = -lsocket -lpthread -lnsl

all: netfileserver


netfileserver: netfileserver.c libnetfiles.h
	$(CC) $(CFLAGS) $(LFLAGS) -o netfileserver netfileserver.c $(LIBS)


clean:
	rm -f *.o netfileserver




