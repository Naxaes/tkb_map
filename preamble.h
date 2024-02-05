#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>


enum LogGroup {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_ASSERT,
    LOG_COUNT,
};

static const char* log_group_names[] = {
        [LOG_INFO]   = "INFO",
        [LOG_WARN]   = "WARN",
        [LOG_ERROR]  = "ERROR",
        [LOG_ASSERT] = "ASSERT",
};

enum LogId {
    LOG_ID_NONE      = 0,
    LOG_ID_GENERAL   = 1 << 0,
    LOG_ID_OPENGL    = 1 << 1,
    LOG_ID_ALLOCATOR = 1 << 2,
    LOG_ID_MACOS     = 1 << 3,
    LOG_ID_ALL       = 0xFFFF
};

#define infof(id, ...)         ((log_filter_passes(LOG_INFO,   id))             && logf_impl(LOG_INFO,   NULL,  __FILE__, __LINE__, __VA_ARGS__))
#define warnf(id, ...)         ((log_filter_passes(LOG_WARN,   id))             && logf_impl(LOG_WARN,   NULL,  __FILE__, __LINE__, __VA_ARGS__))
#define errorf(id, ...)        ((log_filter_passes(LOG_ERROR,  id))             && logf_impl(LOG_ERROR,  NULL,  __FILE__, __LINE__, __VA_ARGS__))
#define assertf(id, cond, ...) ((log_filter_passes(LOG_ASSERT, id) && !(cond))  && logf_impl(LOG_ASSERT, #cond, __FILE__, __LINE__, __VA_ARGS__))

#define logf_at_source(group, id, file, line, ...) (log_filter_passes(group, id) && logf_impl(group, NULL, file, line, __VA_ARGS__))


static /* __thread */ enum LogId log_filter[LOG_COUNT] = {
        [LOG_INFO]   = LOG_ID_ALL,
        [LOG_WARN]   = LOG_ID_ALL,
        [LOG_ERROR]  = LOG_ID_ALL,
        [LOG_ASSERT] = LOG_ID_ALL,
};

static inline void set_log_filter(enum LogGroup group, enum LogId id) {
    log_filter[group] = id;
}

static inline void set_log_filter_all(enum LogId id) {
    for (int i = 0; i < LOG_COUNT; i++)
        log_filter[i] = id;
}

static inline int log_filter_passes(enum LogGroup group, enum LogId id) {
    return (log_filter[group] & id) == id;
}

static int logf_impl(enum LogGroup group, const char* error, const char* file, int line, const char* fmt, ...) {
    FILE* f = (group == LOG_INFO) ? stdout : stderr;

    if (group == LOG_ASSERT)
        fprintf(f, "[%s] %s:%d: %s: ", log_group_names[group], file, line, error);
    else
        fprintf(f, "[%s] %s:%d: ", log_group_names[group], file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    if (group == LOG_ASSERT || group == LOG_ERROR) {
        raise(SIGINT);
        exit(EXIT_FAILURE);
    }

    return 0;
}



#define KILOBYTES(x) (         (x) * 1024ULL)
#define MEGABYTES(x) (KILOBYTES(x) * 1024ULL)
#define GIGABYTES(x) (MEGABYTES(x) * 1024ULL)
#define TERABYTES(x) (GIGABYTES(x) * 1024ULL)
