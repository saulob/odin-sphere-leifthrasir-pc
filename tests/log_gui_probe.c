/* Started by the log tests as a Windows-subsystem program without standard handles, like odin_sphere_leifthrasir.exe
 * from Explorer: <log file> keep|remove. Every way the program writes must reach the run log. Opens no window. */
#include "runtime_log.h"
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static void put(DWORD std_handle, const char *text) {
    DWORD written = 0;
    WriteFile(GetStdHandle(std_handle), text, (DWORD)strlen(text), &written, NULL);
}

int main(int argc, char **argv) {
    char path[4200];
    if (argc != 3) return 2;
    if (!runtime_log_start(NULL, argv[1], path, sizeof(path))) return 3;
    if (_fileno(stdin) < 0 || _fileno(stdout) < 0 || _fileno(stderr) < 0) return 4;
    printf("stdout line\n");
    fprintf(stderr, "stderr line\n");
    if (_write(1, "raw stdout\n", 11) != 11 || _write(2, "raw stderr\n", 11) != 11) return 5;
    put(STD_OUTPUT_HANDLE, "win32 stdout\n");
    put(STD_ERROR_HANDLE, "win32 stderr\n");
    runtime_log_flush();
    if (!strcmp(argv[2], "remove")) {
        runtime_log_set_detailed(0);
        runtime_log_mark_clean_exit();
        runtime_log_finalize();
        printf("after removal\n");
    }
    return 0;
}
