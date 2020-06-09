#ifndef OPTIONS_H_INCLUDED
#define OPTIONS_H_INCLUDED

struct exec_options {
    char* patch_file_path;
    char* patient_file_path;
    int do_respect_post_trunc;
};

struct exec_options* parse_exec_options(int argc, char** argv);
void free_exec_options(struct exec_options* eo);

#endif
