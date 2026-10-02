#ifndef TC_LOG_H
#define TC_LOG_H

#include "tc_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TcLogLevel {
    TC_LOG_TRACE = 0,
    TC_LOG_DEBUG = 1,
    TC_LOG_INFO  = 2,
    TC_LOG_WARN  = 3,
    TC_LOG_ERROR = 4,
    TC_LOG_FATAL = 5
} TcLogLevel;

/* Callback function signature for routing log messages into tc_journal */
typedef void (*TcLogSinkFn)(TcLogLevel level, int64_t ts_ns, const char* module_name, const char* message, void* user_data);

/* Global log dispatcher set by engine during startup */
typedef struct TcLogger {
    TcLogSinkFn sink_fn;
    void*       user_data;
    TcLogLevel  min_level;
} TcLogger;

#ifdef __cplusplus
}
#endif

#endif /* TC_LOG_H */
