#ifndef RUNTIME_LOG_H
#define RUNTIME_LOG_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Creates the next free odin_sphere_leifthrasir_NNN.log in dir, or exactly file when it is given. 0 on failure. */
int runtime_log_create(const char *dir, const char *file, char *path, size_t size, void **handle);

/* Sends stdout and stderr to that log and echoes them to the console the process started with. */
int runtime_log_start(const char *dir, const char *file, char *path, size_t size);

/* Waits until what was written so far has reached the console. */
void runtime_log_flush(void);

#ifdef __cplusplus
}
#endif
#endif
