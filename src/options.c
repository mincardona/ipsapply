#include "options.h"
#include "util.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LONGOPT_ID_PATCH_FILE 1000
#define LONGOPT_ID_PATIENT_FILE 1001
#define LONGOPT_ID_POST_TRUNC 1002
#define LONGOPT_ID_HELP 1003
#define LONGOPT_ID_OUTPUT_FILE 1004
#define LONGOPT_ID_TEXT_PATH 1005

/**
 * Copies a string from src to *dest. If *dest is non-NULL, it is first free()d.
 * Then, enough memory to contain the src string (plus null terminator) is
 * allocated with xmalloc and the pointer to that memory is stored in *dest.
 * Finally, the string at src is copied into the new buffer at *dest.
 * @return the new *dest value
 */
char* clone_string(char** dest, const char* src) {
    if (*dest) {
        free(*dest);
    }
    *dest = xmalloc(strlen(src) + 1);
    return strcpy(*dest, src);
}

struct exec_options* parse_exec_options(int argc, char** argv) {
    const char* shortopts = "p:f:o:x:t";
    struct exec_options* ret = NULL;

    struct option longopts[] = {
        { "patch-path",   required_argument, NULL, LONGOPT_ID_PATCH_FILE },
        { "patient-path", required_argument, NULL, LONGOPT_ID_PATIENT_FILE },
        { "post-trunc",   no_argument,       NULL, LONGOPT_ID_POST_TRUNC },
        { "help",         no_argument,       NULL, LONGOPT_ID_HELP },
        { "output-path",  required_argument, NULL, LONGOPT_ID_OUTPUT_FILE },
        { "text-path",    required_argument, NULL, LONGOPT_ID_TEXT_PATH },
        { 0, 0, 0, 0 }
    };

    ret = xmalloc(sizeof(*ret));
    ret->patch_file_path = NULL;
    ret->patient_file_path = NULL;
    ret->text_file_path = NULL;
    ret->output_file_path = NULL;
    ret->respect_post_trunc = 0;
    ret->help = 0;
    ret->parse_success = 0;
    ret->final_optind = 0;

    opterr = 1; /* print errors on bad options */

    for (;;) {
        int opcode = getopt_long(argc, argv, shortopts, longopts, NULL);

        /* end of options */
        if (opcode == -1) {
            break;
        }

        switch (opcode) {
        case 'p':
        case LONGOPT_ID_PATCH_FILE:
            clone_string(&ret->patch_file_path, optarg);
            break;
        case 'f':
        case LONGOPT_ID_PATIENT_FILE:
            clone_string(&ret->patient_file_path, optarg);
            break;
        case 'o':
        case LONGOPT_ID_OUTPUT_FILE:
            clone_string(&ret->output_file_path, optarg);
            break;
        case 't':
        case LONGOPT_ID_POST_TRUNC:
            ret->respect_post_trunc = 1;
            break;
        case LONGOPT_ID_HELP:
            ret->help = 1;
            break;
        case 'x':
        case LONGOPT_ID_TEXT_PATH:
            clone_string(&ret->text_file_path, optarg);
            break;
        case '?':
        case ':':
        default:
            /* unknown option or missing argument */
            ret->final_optind = optind;
            return ret;
        }
    }

    ret->final_optind = optind;
    ret->parse_success = 1;
    return ret;
}

void free_exec_options(struct exec_options* eo) {
    free(eo->patch_file_path);
    free(eo->patient_file_path);
    free(eo->output_file_path);
    free(eo->text_file_path);
    free(eo);
}
