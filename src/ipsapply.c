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

void print_patch_directive(FILE* f) {
    if (f) {
        fprintf(f, "0x00000000 PATCH\n");
    }
}

void print_hunk_directive(FILE* f, long offset, const struct hunk_header* hunk) {
    if (!f) {
        return;
    }

    fprintf(f, "0x%.8lx ", (unsigned long)offset);

    switch (hunk->type) {
    case HUNK_REGULAR:
        fprintf(f, "REGULAR offset=%#.6x length=%#.4x\n",
            (unsigned)hunk->offset, (unsigned)hunk->length
        );
        break;
    case HUNK_RLE:
        fprintf(f, "RLE offset=%#.6x length=%#.4x fill=%#.2x\n",
            (unsigned)hunk->offset, (unsigned)hunk->length,
            (unsigned)hunk->fill
        );
        break;
    default: /* HUNK_EOF */
        fprintf(f, "EOF\n");
        break;
    }
}

void print_trunc_directive(FILE* f, int trunc_length) {
    fprintf(f, "TRUNCATE length=%#.6x\n", (unsigned)trunc_length);
}

int patch_parse(
    struct exec_options* eo,
    FILE* patch_file,
    FILE* text_file,
    FILE* patient_file,
    FILE* output_file)
{
    int magic_patch_found = 0;
    int code = 0;
    int warned_ftell_failure = 0;
    struct hunk_header hunk = { HUNK_EOF, 0, 0, '\0' };
    unsigned char* pay_buf = NULL;

    /* check some preconditions */
    if (patch_file == NULL || ((patient_file == NULL) != (output_file == NULL))) {
        fprintf(stderr, "error: invalid file combination in patch_parse\n");
        goto ERROR;
    }

    pay_buf = xmalloc((1 << (8 * HUNK_LENGTH_WIDTH)) - 1);

    /* copy the patient file to the output file to start */
    if (patient_file) {
        if (copy_file(patient_file, output_file)
            || fseek(patient_file, 0, SEEK_SET)
            || fseek(output_file, 0, SEEK_SET))
        {
            fprintf(stderr, "error: failed to copy patient data to output file\n");
            goto ERROR;
        }
    }

    /* parse the patch file */

    /* read magic PATCH */
    code = read_magic_patch(patch_file, &magic_patch_found);
    if (code) {
        fprintf(stderr, "error: while detecting magic PATCH: "
            "%s\n", FILE_CODE_STR[code]);
        goto ERROR;
    } else if (!magic_patch_found) {
        fprintf(stderr, "error: magic PATCH not found\n");
        goto ERROR;
    }
    print_patch_directive(text_file);

    /* read hunks */
    for (;;) {
        long told = ftell(patch_file);
        if (told == -1L) {
            if (!warned_ftell_failure) {
                fprintf(stderr, "warning: failed ftell on input file: %s\n",
                    strerror(errno));
                warned_ftell_failure = 1;
            }
            told = LONG_MAX;
        }

        code = read_hunk_header(patch_file, &hunk);
        if (code) {
            fprintf(stderr, "error: while reading hunks: %s\n",
                FILE_CODE_STR[code]);
            goto ERROR;
        }

        /* print offset (in patch file) of current hunk directive */
        print_hunk_directive(text_file, (unsigned long)told, &hunk);

        if (hunk.type == HUNK_EOF) {
            break;
        }

        if (hunk.length == 0) {
            fprintf(stderr, "warning: hunk with length 0\n");
        }

        if (hunk.type == HUNK_REGULAR) {
            if (output_file) {
                if (fread(pay_buf, 1, hunk.length, patch_file) < (size_t)hunk.length) {
                    fprintf(
                        stderr,
                        "error: while reading hunk payload: %s\n",
                        FILE_CODE_STR[FILE_CODE(patch_file)]
                    );
                    goto ERROR;
                }
                if (fseek(output_file, hunk.offset, SEEK_SET)) {
                    fprintf(stderr, "error: unable to seek to hunk payload"
                        " offset in patient file");
                    goto ERROR;
                }
                if (fwrite(pay_buf, 1, hunk.length, output_file) < (size_t)hunk.length) {
                    fprintf(
                        stderr,
                        "error: while writing hunk payload: %s\n",
                        FILE_CODE_STR[FILE_CODE(output_file)]
                    );
                    goto ERROR;
                }
            } else {
                /* skip the hunk payload because we aren't applying it */
                if (fseek(patch_file, hunk.length, SEEK_CUR)) {
                    fprintf(stderr, "error: while reading hunks:"
                        " failed to seek past hunk payload\n");
                    goto ERROR;
                }
            }
        } else { /* HUNK_RLE */
            if (output_file) {
                if (fseek(output_file, hunk.offset, SEEK_SET)) {
                    fprintf(stderr, "error: unable to seek to RLE hunk payload"
                        " offset in patient file");
                    goto ERROR;
                }
                memset(pay_buf, hunk.fill, hunk.length);
                if (fwrite(pay_buf, 1, hunk.length, output_file) < (size_t)hunk.length) {
                    fprintf(stderr,
                        "error: while writing RLE hunk payload: %s\n",
                        FILE_CODE_STR[FILE_CODE(output_file)]
                    );
                    goto ERROR;
                }
            }
        }
    }

    /* optional truncation */
    if (eo->respect_post_trunc) {
        int expect_eof_char = EOF;
        int trunc_length = 0;
        code = read_trunc_length(patch_file, &trunc_length);
        if (code) {
            fprintf(stderr, "error: while reading truncation length: %s\n",
                FILE_CODE_STR[code]);
            goto ERROR;
        } else if (trunc_length >= 0) {
            print_trunc_directive(text_file, trunc_length);
            if (output_file && truncate_file(output_file, trunc_length)) {
                fprintf(stderr, "error: failed to truncate file");
                goto ERROR;
            }
        }

        /* test for additional unexpected data and issue a warning.
           we only do this if we were instructed to check for a truncation
           length after the EOF marker */
        expect_eof_char = getc(patch_file);
        if (expect_eof_char == EOF && !feof(patch_file)) {
            fprintf(stderr, "error: while checking for"
                " post-truncation-length data: %s\n", FILE_CODE_STR[FILE_CODE(patch_file)]);
        } else if (expect_eof_char != EOF) {
            ungetc(expect_eof_char, patch_file);
            /* unexpected data at the tail of the file */
            fprintf(stderr, "warning: unexpected bytes at end of file."
                " ignoring...\n");
        }
    }

    /* cleanup */
    free(pay_buf);
    return EXIT_SUCCESS;

ERROR:
    free(pay_buf);
    return EXIT_FAILURE;
}

/**
 * A wrapper for fopen(path, "rb") that treats the path "-" as stdin.
 * This is intended for opening patch files.
 */
FILE* fopen_patch(const char* path) {
    if (!path) {
        return NULL;
    } else if (STREQ(path, "-")) {
        return stdin;
    } else {
        return fopen(path, "rb");
    }
}

FILE* fopen_patient(const char* path) {
    return fopen_patch(path);
}

FILE* fopen_text(const char* path) {
    if (!path || STREQ(path, "-")) {
        return stdout;
    } else {
        return fopen(path, "w");
    }
}

FILE* fopen_output(const char* path) {
    if (!path) {
        return NULL;
    }
    return fopen(path, "wb");
}

int fclose_check(FILE* f) {
    if (f && f != stdin && f != stdout) {
        return fclose(f);
    }
    return 0;
}

int subcommand_apply(struct exec_options* eo) {
    int return_code = EXIT_FAILURE;
    FILE* patch_file = NULL;
    FILE* text_file = NULL;
    FILE* patient_file = NULL;
    FILE* output_file = NULL;

    /* text file is optional for this subcommand */
    if (eo->text_file_path && !(text_file = fopen_text(eo->text_file_path))) {
        fprintf(stderr, "error: failed to open text file\n");
        goto ERROR;
    }

    if (!(patch_file = fopen_patch(eo->patch_file_path))) {
        fprintf(stderr, "error: failed to open patch file\n");
        goto ERROR;
    }

    if (!(patient_file = fopen_patient(eo->patient_file_path))) {
        fprintf(stderr, "error: failed to open patient file\n");
        goto ERROR;
    }

    if (!(output_file = fopen_output(eo->output_file_path))) {
        fprintf(stderr, "error: failed to open output file\n");
        goto ERROR;
    }

    return_code = patch_parse(eo, patch_file, text_file, patient_file, output_file);

    fclose_check(patch_file);
    patch_file = NULL;
    fclose_check(patient_file);
    patient_file = NULL;

    if (fclose_check(text_file)) {
        text_file = NULL;
        fprintf(stderr, "error: unable to close text file\n");
        goto ERROR;
    }
    text_file = NULL;

    if (fclose_check(output_file)) {
        output_file = NULL;
        fprintf(stderr, "error: unable to close output file\n");
        goto ERROR;
    }
    output_file = NULL;

    return return_code;

ERROR:
    fclose_check(patch_file);
    fclose_check(patient_file);
    fclose_check(text_file);
    fclose_check(output_file);
    return EXIT_FAILURE;
}

int subcommand_text(struct exec_options* eo) {
    int return_code = EXIT_FAILURE;
    FILE* patch_file = NULL;
    FILE* text_file = NULL;
    
    if (!(patch_file = fopen_patch(eo->patch_file_path))) {
        fprintf(stderr, "error: failed to open patch file\n");
        goto ERROR;
    }

    if (!(text_file = fopen_text(eo->text_file_path))) {
        fprintf(stderr, "error: failed to open text file\n");
        goto ERROR;
    }

    return_code = patch_parse(eo, patch_file, text_file, NULL, NULL);

    fclose_check(patch_file);
    patch_file = NULL;

    if (fclose_check(text_file)) {
        text_file = NULL;
        fprintf(stderr, "error: unable to close text file\n");
        goto ERROR;
    }

    return return_code;

ERROR:
    fclose_check(patch_file);
    fclose_check(text_file);
    return EXIT_FAILURE;
}

int main(int argc, char** argv) {
    struct exec_options* eo = NULL;
    int exit_code = EXIT_SUCCESS;

    eo = parse_exec_options(argc, argv);
    if (!eo->parse_success) {
        fprintf(stderr, "%s\n", "Error parsing options."
            " Use `ipsa --help` to view help.");
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
            fprintf(stderr, "%s\n", "No subcommand given."
                " Use `ipsa --help` to view help.");
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
