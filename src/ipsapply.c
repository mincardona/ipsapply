#include "config_ipsapply.h"
#include "options.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

int subcommand_apply(struct exec_options* eo) {
    (void)eo;
    printf("nop\n");
    return EXIT_SUCCESS;
}

int subcommand_text(struct exec_options* eo) {
    (void)eo;
    printf("nop\n");
    return EXIT_SUCCESS;
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
