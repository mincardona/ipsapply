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

int copy_file(FILE* src, FILE* dest) {
    /* there are OS-specific functions for this,
       but a read/write loop works for now */

    const size_t buflen = 4096;
    unsigned char* buf = xmalloc(buflen);

    int done = 0;
    while (!done) {
        size_t chars_read = fread(buf, 1, buflen, src);
        if (chars_read < buflen) {
            if (feof(src)) {
                /* write the last (partial) block, then end the loop */
                done = 1;
            } else {
                goto ERROR;
            }
        }
        if (fwrite(buf, 1, chars_read, dest) < chars_read) {
            goto ERROR;
        }
    }

    free(buf);
    return 0;

ERROR:
    free(buf);
    return -1;
}

/*******************************************************************************
CRC32 functions
This implementation is based on the algorithm from Annex D of the Portable
Network Graphics (PNG) Specification (Second Edition)
<https://www.w3.org/TR/PNG>
*******************************************************************************/
static uint32_t crc32_memo[0x100];

void crc32_init() {
    const uint32_t poly = 0xedb88320u;
    uint32_t pre;
    uint32_t i;
    for (i = 0; i < 0x100; i++) {
        int j;
        pre = i;
        for (j = 0; j < 8; j++) {
            if (pre & 1) {
                pre = poly ^ (pre >> 1);
            } else {
                pre >>= 1;
            }
        }
        crc32_memo[i] = pre;
    }
}

uint32_t crc32_update(uint32_t crc_prev, void* buf, size_t len) {
    uint32_t crc_next = crc_prev;
    size_t i;
    for (i = 0; i < len; i++) {
        crc_next = crc32_memo[(crc_next ^ ((unsigned char*)buf)[i]) & 0xff] ^ (crc_next >> 8);
    }
    return crc_next;
}

uint32_t crc32_finalize(uint32_t crc_prev) {
    return crc_prev ^ CRC32_BASE;
}

uint32_t crc32_quick(void* buf, size_t len) {
    return crc32_finalize(crc32_update(CRC32_BASE, buf, len));
}
