#include "util.h"
#include <stdio.h>
#include <stdlib.h>

void* xmalloc(size_t size) {
    void* ret = malloc(size);
    if (!ret) {
        fprintf(stderr, "memory exhausted allocating %lu bytes\n", (unsigned long)size);
        abort();
    }
    return ret;
}
