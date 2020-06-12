#include "config_ipsapply.h"
#include "options.h"
#include "util.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HUNK_OFFSET_WIDTH 3
#define HUNK_LENGTH_WIDTH 2
#define TRUNC_LENGTH_WIDTH 3

#define FILE_CODE_OK 0
#define FILE_CODE_EARLY_EOF 1
#define FILE_CODE_ERROR 2

const char* FILE_CODE_STR[] = {
    "ok",
    "unexpected EOF",
    "I/O error",
    /* add new strings between these */
    "FILE_CODE_STR bounds error"
};

#define FILE_CODE(F) (feof(F) ? FILE_CODE_EARLY_EOF : FILE_CODE_ERROR)

const char EOF_MARKER[] = "EOF";
const char MAGIC_PATCH[] = "PATCH";

enum hunk_header_type {
    HUNK_REGULAR,
    HUNK_RLE,
    HUNK_EOF
};

struct hunk_header {
    enum hunk_header_type type;

    int offset;
    int length;
    unsigned char fill;   /* RLE only */
};

int decode_big_endian_3(unsigned char bytes[3]) {
    return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}

int decode_big_endian_2(unsigned char bytes[2]) {
    return (bytes[0] << 8) | bytes[1];
}

int read_hunk_header(FILE* f, struct hunk_header* h) {
    /* all hunk header fields are 3 bytes wide at most */
    unsigned char buf[3] = { 0 };
    size_t chars_read = 0;

    /* get offset or EOF marker */
    chars_read = fread(buf, 1, HUNK_OFFSET_WIDTH, f);
    if (chars_read < HUNK_OFFSET_WIDTH) {
        return FILE_CODE(f);
    } else if (MEMEQ(buf, EOF_MARKER, HUNK_OFFSET_WIDTH)) {
        h->type = HUNK_EOF;
        return FILE_CODE_OK;
    } else {
        h->offset = decode_big_endian_3(buf);
    }

    /* get regular length */
    chars_read = fread(buf, 1, HUNK_LENGTH_WIDTH, f);
    if (chars_read < HUNK_LENGTH_WIDTH) {
        return FILE_CODE(f);
    }
    h->length = decode_big_endian_2(buf);
    if (h->length != 0) {
        h->type = HUNK_REGULAR;
        return FILE_CODE_OK;
    } else {
        h->type = HUNK_RLE;
    }

    /* at this point, this is an RLE hunk */
    /* read the RLE length */
    chars_read = fread(buf, 1, HUNK_LENGTH_WIDTH, f);
    if (chars_read < HUNK_LENGTH_WIDTH) {
        return FILE_CODE(f);
    }
    h->length = decode_big_endian_2(buf);

    /* read the RLE fill byte */
    chars_read = fread(buf, 1, 1, f);
    if (chars_read < 1) {
        return FILE_CODE(f);
    }
    h->fill = buf[0];

    return FILE_CODE_OK;
}

int read_magic_patch(FILE* f, int* found) {
    char buf[6] = { 0 };
    size_t chars_read = fread(buf, 1, 5, f);
    if (chars_read < 5) {
        return FILE_CODE(f);
    }
    *found = STREQ(MAGIC_PATCH, buf);
    return FILE_CODE_OK;
}

/* if the function succeeds, and length is -1, no post data was provided */
int read_trunc_length(FILE* f, int* length) {
    unsigned char buf[3] = { 0 };
    size_t chars_read = fread(buf, 1, TRUNC_LENGTH_WIDTH, f);

    if (chars_read == 0 && FILE_CODE(f) == FILE_CODE_EARLY_EOF) {
        /* it's OK if no data at all exists past the EOF marker */
        *length = -1;
    } else if (chars_read < TRUNC_LENGTH_WIDTH) {
        return FILE_CODE(f);
    } else {
        *length = decode_big_endian_3(buf);
    }

    return FILE_CODE_OK;
}

int subcommand_apply(struct exec_options* eo) {
    (void)eo;
    printf("nop\n");
    return EXIT_SUCCESS;
}

int subcommand_text(struct exec_options* eo) {
    FILE* input_file = NULL;
    FILE* output_file = NULL;
    int magic_patch_found = 0;
    int code = 0;
    int trunc_length = 0;
    int expect_eof_char = EOF;
    int warned_ftell_failure = 0;
    struct hunk_header hunk = { HUNK_EOF, 0, 0, '\0' };

    if (!eo->patch_file_path) {
        fprintf(stderr, "error: no patch file provided\n");
        return EXIT_FAILURE;
    }

    if (STREQ(eo->patch_file_path, "-")) {
        input_file = stdin;
    } else {
        input_file = fopen(eo->patch_file_path, "rb");
        if (input_file == NULL) {
            fprintf(stderr, "error: failed to open patch file\n");
            goto ERROR;
        }
    }

    if (!eo->output_file_path || STREQ(eo->output_file_path, "-")) {
        output_file = stdout;
    } else {
        output_file = fopen(eo->output_file_path, "wb");
        if (output_file == NULL) {
            fprintf(stderr, "error: failed to open output file\n");
            goto ERROR;
        }
    }

    /* parse the patch file */

    /* read magic PATCH */
    code = read_magic_patch(input_file, &magic_patch_found);
    if (code) {
        fprintf(stderr, "error: while detecting magic PATCH: %s\n", FILE_CODE_STR[code]);
        goto ERROR;
    } else if (!magic_patch_found) {
        fprintf(stderr, "error: magic PATCH not found\n");
        goto ERROR;
    } else {
        fprintf(output_file, "0x00000000 PATCH\n");
    }

    /* read hunks */
    for (;;) {
        long told = ftell(input_file);
        if (told == -1L) {
            if (!warned_ftell_failure) {
                fprintf(stderr, "warning: failed ftell on input file: %s\n", strerror(errno));
                warned_ftell_failure = 1;
            }
            told = LONG_MAX;
        }

        code = read_hunk_header(input_file, &hunk);
        if (code) {
            fprintf(stderr, "error: while reading hunks: %s\n", FILE_CODE_STR[code]);
            goto ERROR;
        }

        fprintf(output_file, "0x%.8lx ", (unsigned long)told);
        if (hunk.type == HUNK_EOF) {
            fprintf(output_file, "EOF\n");
            break;
        } else if (hunk.type == HUNK_REGULAR) {
            fprintf(
                output_file,
                "REGULAR offset=%#.6x length=%#.4x\n",
                (unsigned)hunk.offset, (unsigned)hunk.length
            );
            /* skip the hunk payload */
            if (fseek(input_file, hunk.length, SEEK_CUR)) {
                fprintf(stderr, "error: while reading hunks: failed to seek past a hunk payload\n");
                goto ERROR;
            }
        } else /* HUNK_RLE */ {
            fprintf(
                output_file,
                "RLE offset=%#.6x length=%#.4x fill=%#.2x\n",
                (unsigned)hunk.offset, (unsigned)hunk.length, (unsigned)hunk.fill
            );
        }
    }

    /* optional truncation */
    if (eo->respect_post_trunc) {
        code = read_trunc_length(input_file, &trunc_length);
        if (code) {
            fprintf(stderr, "error: while reading truncation length: %s\n", FILE_CODE_STR[code]);
            goto ERROR;
        } else if (trunc_length >= 0) {
            fprintf(output_file, "TRUNCATE length=%#.6x\n", (unsigned)trunc_length);
        }

        /* test for additional unexpected data and issue a warning.
           we only do this if we were instructed to check for a truncation
           length after the EOF marker */
        expect_eof_char = getc(input_file);
        if (expect_eof_char == EOF && !feof(input_file)) {
            fprintf(stderr, "error: while checking for post-truncation-length data: I/O error\n");
        } else if (expect_eof_char != EOF) {
            ungetc(expect_eof_char, input_file);
            /* unexpected data at the tail of the file */
            fprintf(stderr, "warning: unexpected bytes at end of file. ignoring...\n");
        }
    }

    /* cleanup */

    if (input_file != stdin) {
        fclose(input_file);
        input_file = NULL;
    }

    if (output_file == stdout) {
        if (fflush(output_file) == EOF) {
            fprintf(stderr, "error: unable to flush output file\n");
            goto ERROR;
        }
    } else {
        if (fclose(output_file) == EOF) {
            output_file = NULL;
            fprintf(stderr, "error: unable to close output file\n");
            goto ERROR;
        }
    }

    return EXIT_SUCCESS;

ERROR:
    if (input_file && input_file != stdin) {
        fclose(input_file);
    }
    if (output_file && output_file != stdout) {
        fclose(output_file);
    }
    return EXIT_FAILURE;
}

int main(int argc, char** argv) {
    struct exec_options* eo = NULL;
    int exit_code = EXIT_SUCCESS;

    eo = parse_exec_options(argc, argv);
    if (!eo->parse_success) {
        fprintf(stderr, "%s\n", "Error parsing options. Use `ipsa --help` to view help.");
        exit_code = EXIT_FAILURE;
    } else if (eo->help) {
        printf(
            "%s - %s\n",
            "ipsa - IPS patcher utility",
            "Copyright (C) 2020 Michael Incardona"
        );
        printf("version %s (GPLv3)\n", IPSAPPLY_VERSION_STR);
        printf("--------\n");
        printf("help text not available yet\n");
    } else {
        char* subcommand = argv[eo->final_optind];
        if (!subcommand) {
            fprintf(stderr, "%s\n", "No subcommand given. Use `ipsa --help` to view help.");
            exit_code = EXIT_FAILURE;
        } else if (STREQ(subcommand, "apply")) {
            exit_code = subcommand_apply(eo);
        } else if (STREQ(subcommand, "text")) {
            exit_code = subcommand_text(eo);
        }
    }

    free_exec_options(eo);
    return exit_code;
}
