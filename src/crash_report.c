#include "crash_report.h"
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

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

const char *crash_exception_name(uint32_t code) {
    switch (code) {
    case 0x80000002u: return "DATATYPE_MISALIGNMENT";
    case 0xC0000005u: return "ACCESS_VIOLATION";
    case 0xC0000006u: return "IN_PAGE_ERROR";
    case 0xC0000008u: return "STATUS_INVALID_HANDLE";
    case 0xC0000017u: return "STATUS_NO_MEMORY";
    case 0xC000001Du: return "ILLEGAL_INSTRUCTION";
    case 0xC0000025u: return "STATUS_NONCONTINUABLE_EXCEPTION";
    case 0xC0000094u: return "INTEGER_DIVIDE_BY_ZERO";
    case 0xC0000095u: return "INTEGER_OVERFLOW";
    case 0xC0000096u: return "PRIVILEGED_INSTRUCTION";
    case 0xC00000FDu: return "STATUS_STACK_OVERFLOW";
    case 0xC000013Au: return "STATUS_CONTROL_C_EXIT";
    case 0xC0000374u: return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409u: return "STATUS_STACK_BUFFER_OVERRUN";
    case 0xE06D7363u: return "C++ EXCEPTION";
    default: return NULL;
    }
}

const char *crash_signal_name(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
#ifdef SIGBUS
    case SIGBUS: return "SIGBUS";
#endif
    default: return NULL;
    }
}

size_t crash_report_format(const CrashReport *report, char *out, size_t size) {
    if (!out || !size) return 0;
    out[0] = 0;
    Text text = {out, size, 0};
    const unsigned long long seconds = (unsigned long long)(report->uptime_ms / 1000);
    add(&text, CRASH_REPORT_RULE "CRASH REPORT\n" CRASH_REPORT_RULE "%s\n", report->event ? report->event : "Fatal error");
    if (report->reason) add(&text, "Reason: %s\n", report->reason);
    add(&text, "Product: Odin Sphere Leifthrasir PC %s\n", report->version ? report->version : "unknown");
    if (report->game_build) add(&text, "Game build: %s\n", report->game_build);
    add(&text, "Uptime: %02llu:%02llu:%02llu\n", seconds / 3600, seconds / 60 % 60, seconds % 60);
    if (report->signal) {
        const char *name = crash_signal_name(report->signal);
        add(&text, "Signal: %d (%s), code %d\n", report->signal, name ? name : "unknown", report->signal_code);
        add(&text, "Instruction address: 0x%016llX\n", (unsigned long long)report->address);
    } else {
        const char *name = crash_exception_name(report->code);
        if (name) add(&text, "Exception code: 0x%08X (%s)\n", (unsigned)report->code, name);
        else add(&text, "Exception code: 0x%08X\n", (unsigned)report->code);
        add(&text, "Exception address: 0x%016llX\n", (unsigned long long)report->address);
        add(&text, "Exception flags: 0x%08X\n", (unsigned)report->flags);
    }
    add(&text, "Faulting module: %s\n", report->location ? report->location : "unknown");
    if (!report->signal) {
        const uint32_t count = report->parameter_count < CRASH_REPORT_MAX_PARAMETERS ? report->parameter_count
                                                                                     : CRASH_REPORT_MAX_PARAMETERS;
        add(&text, "Exception parameters: %u\n", (unsigned)count);
        for (uint32_t i = 0; i < count; ++i)
            add(&text, "  parameter[%u]: 0x%016llX\n", (unsigned)i, (unsigned long long)report->parameters[i]);
        if ((report->code == 0xC0000005u || report->code == 0xC0000006u) && count >= 2) {
            const uint64_t kind = report->parameters[0];
            const char *access = kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : NULL;
            if (access) add(&text, "Memory access: %s\n", access);
            add(&text, "Referenced address: 0x%016llX\n", (unsigned long long)report->parameters[1]);
        }
    } else if (report->has_referenced_address) {
        add(&text, "Referenced address: 0x%016llX\n", (unsigned long long)report->referenced_address);
    }
    add(&text, "Thread ID: %llu\nProcess ID: %llu\n", (unsigned long long)report->thread_id,
        (unsigned long long)report->process_id);
    return text.used;
}
