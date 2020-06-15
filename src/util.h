#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Like malloc(), but abort()s with a message on stderr if allocation fails.
 */
void* xmalloc(size_t size);

/* Truthy if two string compare equal */
#define STREQ(A, B) (!strcmp((A), (B)))

#define MEMEQ(A, B, L) (!memcmp((A), (B), (L)))

/**
 * Truncates a file to a certain number of bytes in length, then seeks to the
 * end of the file. Returns 0 on success or nonzero on error.
 */
int truncate_file(FILE* f, int bytes);

/**
 * Copies all data from src to dest. Data is read from src and written to dest
 * starting at the current file offset. Returns 0 on success or nonzero on
 * error.
 */
int copy_file(FILE* src, FILE* dest);

#endif
