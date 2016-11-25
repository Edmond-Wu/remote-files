#ifndef LIBNETFILES_H
#define LIBNETFILES_H
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>

int netopen(const char *pathname, int flags);

size_t netread(int files, void *buf, size_t nbytes);

size_t netwrite(int files, const void *buf, size_t nbytes);

int netclose(int fd);
#endif
