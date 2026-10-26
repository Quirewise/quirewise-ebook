#ifndef QUIREWISE_WORKSPACE_H
#define QUIREWISE_WORKSPACE_H

#include <stddef.h>
#include <stdio.h>

#define QW_WORKSPACE_PATH_CAP 4096u

/* Creates a unique retained directory below QUIREWISE_TMP_DIR/work. */
int qw_workspace_create(char *out_path,
                       size_t capacity,
                       const char *purpose);

int qw_workspace_path_join(char *out_path,
                           size_t capacity,
                           const char *left,
                           const char *right);

/* Opens a UTF-8 path on Windows and a native path on POSIX systems. */
FILE *qw_workspace_open_file(const char *path, const char *mode);

/* Creates a new file exclusively, failing if the path already exists. */
FILE *qw_workspace_open_exclusive_file(const char *path);

/* Removes a file using UTF-8 paths. */
int qw_workspace_remove_file(const char *path);

#endif
