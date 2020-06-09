#include "options.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

struct exec_options* parse_options(int argc, char** argv) {
    struct exec_options* ret = malloc(sizeof(*ret));
    (void)argc;
    (void)argv;
    ret->patch_file_path = NULL;
    ret->patient_file_path = NULL;
    ret->do_respect_post_trunc = 0;
    return ret;
}

void free_exec_options(struct exec_options* eo) {
    free(eo->patch_file_path);
    eo->patch_file_path = NULL;
    free(eo->patient_file_path);
    eo->patient_file_path = NULL;
    free(eo);
}
