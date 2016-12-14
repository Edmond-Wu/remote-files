
CC     = gcc
CFLAGS = -g -Wall -pedantic -ansi -pthread -std=c11
LIBS   = -lpthread -lnsl
OBJS   = libnetfiles.o 



all: netfileserver libnetfiles.o


netfileserver: netfileserver.c libnetfiles.h
	$(CC) $(CFLAGS) -o netfileserver netfileserver.c $(LIBS)


libnetfiles.o: libnetfiles.c libnetfiles.h
	$(CC) $(CFLAGS) -c libnetfiles.c

clean:
	rm -f *.o netfileserver


