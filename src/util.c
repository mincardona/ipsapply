#if defined(__linux__)
    #define _POSIX_C_SOURCE 200809L
    #include <unistd.h>
    #include <sys/types.h>
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #error "Must be compiled on/for Linux or Windows"
#endif

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

int truncate_file(FILE* f, int bytes) {
    if (bytes < 0) {
        return -1;
    }
#if defined(__linux__)
    {
        int fd = fileno(f);
        if (fd < 0) {
            return -1;
        }
        if (ftruncate(fd, (off_t)bytes)) {
            return -1;
        }
        fseek(f, 0, SEEK_END);
    }
#elif defined(_WIN32)
    {
        int fd = _fileno(f);
        if (fd < 0) {
            return -1;
        }
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        if (h == INVALID_HANDLE_VALUE) {
            return -1;
        }
        /* may need to use SetFilePointer on the HANDLE if this doesn't work. */
        /* may also need to add or subtract 1 from bytes? */
        fseek(f, bytes, SEEK_SET);
        SetEndOfFile(h);
        /* we already fseek()ed to the new end of the file. */
        /* no need to close the fd or HANDLE per the _get_osfhandle() docs */
    }
#endif
    return 0;
}
