#include "hang_report.h"
#include "crash_report.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *out;
    size_t size, used;
} Text;

__attribute__((format(printf, 2, 3))) static void add(Text *text, const char *format, ...) {
    if (text->used + 1 >= text->size) return;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(text->out + text->used, text->size - text->used, format, args);
    va_end(args);
    if (n <= 0) return;
    const size_t room = text->size - text->used - 1;
    text->used += (size_t)n < room ? (size_t)n : room;
}

size_t hang_report_format(const HangReport *report, char *out, size_t size) {
    if (!out || !size) return 0;
    out[0] = 0;
    Text text = {out, size, 0};
    const unsigned long long seconds = (unsigned long long)(report->uptime_ms / 1000);
    add(&text, CRASH_REPORT_RULE "HANG REPORT\n" CRASH_REPORT_RULE "The game stopped responding; it did not crash.\n");
    add(&text, "Reason: %s\n", report->reason ? report->reason : "unknown");
    add(&text, "Product: Odin Sphere Leifthrasir PC %s\n", report->version ? report->version : "unknown");
    if (report->game_build) add(&text, "Game build: %s\n", report->game_build);
    if (report->os) add(&text, "OS: %s\n", report->os);
    if (report->time) add(&text, "Time: %s\n", report->time);
    add(&text, "Uptime: %02llu:%02llu:%02llu\n", seconds / 3600, seconds / 60 % 60, seconds % 60);
    add(&text, "Process ID: %llu\n", (unsigned long long)report->process_id);
    if (report->session_log) add(&text, "Session log: %s\n", report->session_log);
    add(&text, "Frames presented: %llu\n", (unsigned long long)report->frames);
    add(&text, "Threads: %u, %u reported (the watchdog's own are left out)\n", report->threads, report->reported);
    return text.used;
}

size_t hang_report_format_thread(const HangThread *thread, char *out, size_t size) {
    if (!out || !size) return 0;
    out[0] = 0;
    Text text = {out, size, 0};
    add(&text, "\nThread %llu", (unsigned long long)thread->id);
    if (thread->name && *thread->name) {
        char name[64];
        size_t length = 0;
        for (const char *c = thread->name; *c && length < sizeof(name) - 1; ++c)
            name[length++] = (unsigned char)*c < 0x20 || *c == '"' ? '?' : *c;
        name[length] = 0;
        add(&text, " \"%s\"", name);
    }
    if (thread->window) add(&text, " (game window)");
    add(&text, "\n");
    if (thread->note) add(&text, "  (%s)\n", thread->note);
    const unsigned count = thread->frame_count < HANG_REPORT_MAX_FRAMES ? thread->frame_count : HANG_REPORT_MAX_FRAMES;
    for (unsigned i = 0; i < count && thread->frames; ++i)
        add(&text, "  #%02u %s\n", i, thread->frames[i] ? thread->frames[i] : "?");
    return text.used;
}

int hang_report_file_name(unsigned number, char *out, size_t size) {
    const int n = snprintf(out, size, "hang_%03u.txt", number);
    return n > 0 && (size_t)n < size;
}

int hang_report_number(const char *name, unsigned *number) {
    int used = 0;
    return sscanf(name, "hang_%u.txt%n", number, &used) == 1 && used > 0 && !name[used];
}

const char *hang_report_base_name(const char *path) {
    const char *base = path;
    for (const char *c = path; *c; ++c)
        if (*c == '/' || *c == '\\') base = c + 1;
    return base;
}
