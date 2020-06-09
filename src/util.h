#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stdlib.h>
#include <string.h>

/**
 * Like malloc(), but abort()s with a message on stderr if allocation fails.
 */
void* xmalloc(size_t size);

/* Truthy if two string compare equal */
#define STREQ(A, B) (!strcmp((A), (B)))

#endif
