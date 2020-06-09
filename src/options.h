#ifndef OPTIONS_H_INCLUDED
#define OPTIONS_H_INCLUDED

struct exec_options {
    char* patch_file_path;
    char* patient_file_path;
    char* output_file_path;
    int respect_post_trunc;
    int help;

    int parse_success;
    int final_optind;
};

struct exec_options* parse_exec_options(int argc, char** argv);
void free_exec_options(struct exec_options* eo);

#endif
