//
// Created by eugene on 4/15/2020.
//

#ifndef ZITI_SDK_ZITI_LOG_H
#define ZITI_SDK_ZITI_LOG_H

#include <uv.h>

#include "externs.h"

#ifdef SOURCE_PATH_SIZE
#define __FILENAME__ (&__FILE__[SOURCE_PATH_SIZE])
#else
#define __FILENAME__ __FILE__
#endif

#ifndef ZITI_LOG_PREFIX
#define ZITI_LOG_PREFIX
#endif

#define _to_str(x) #x
#define to_str(x) _to_str(x)
// for windows compilation NOGDI needs to be set:
// right click ziti -> properties -> C/C++ -> Preprocessor - ensure NOGDI is in the list of preprocessor definitions
// if it's not present check the CMakeLists.txt file
#define DEBUG_LEVELS(XX) \
    XX(NONE) \
    XX(ERROR) /*WINDOWS - see comment above wrt NOGDI*/ \
    XX(WARN) \
    XX(INFO) \
    XX(DEBUG) \
    XX(VERBOSE) \
    XX(TRACE)

enum DebugLevel {
#define _level(n) n,
    DEBUG_LEVELS(_level)
#undef _level
};

#ifndef ZITI_LOG_MODULE
#define ZITI_LOG_MODULE NULL
#endif

#define ZITI_LOG(level, fmt, ...) do { \
if (level <= ziti_log_level()) { ziti_logger(level, ZITI_LOG_MODULE, __FILENAME__, __LINE__, __func__, fmt, ##__VA_ARGS__); }\
} while(0)

#ifdef __cplusplus
extern "C" {
#endif

#if _MSC_VER >= 1400
# include <sal.h>
# if _MSC_VER > 1400
#  define FORMAT_STRING(p) _Printf_format_string_ p
# else
#  define FORMAT_STRING(p) __format_string p
# endif /* FORMAT_STRING */
#else
# define FORMAT_STRING(p) p
#endif /* _MSC_VER */

#ifdef __GNUC__
#define ziti_printf_args(a, b) __attribute__((__format__ (printf,a,b)))
#else
#define ziti_printf_args(a,b)
#endif

#define ZITI_LOG_DEFAULT_LEVEL (-1)

typedef void (*log_writer)(int level, const char *loc, const char *msg, size_t msglen);

ZITI_FUNC extern void
ziti_logger(int level, const char *module, const char *file, unsigned int line, const char *func,
            FORMAT_STRING(const char *fmt), ...)
ziti_printf_args(6, 7)
;

// call once
// use ZITI_LOG_DEFAULT_LEVEL to use default(INFO)/ZITI_LOG env var
// pass logger = NULL to use default output
ZITI_FUNC extern void ziti_log_init(uv_loop_t *loop, int level, log_writer logger);

ZITI_FUNC extern void ziti_log_set_logger(log_writer logger);

// use ZITI_LOG_DEFAULT_LEVEL to reset to default(INFO) or ZITI_LOG env var
ZITI_FUNC extern void ziti_log_set_level(int level);

// don't use directly
ZITI_FUNC extern int ziti_log_level();

ZITI_FUNC extern void ziti_log_set_level_by_label(const char* log_level);

ZITI_FUNC extern const char* ziti_log_level_label();

/**
 * can be used to turn on logging of uv-mbed library and send log messages into the ziti_log
 * Usage: <code>uv_mbed_set_debug(level, uv_mbed_logger);</code>
 */
ZITI_FUNC void uv_mbed_logger(int level, const char *file, unsigned int line, const char *msg);

// don't use this function. it's a temporary function to support logging on windows.it will be
// removed in a future release
ZITI_FUNC void ziti_enable_uv_mbed_logger(int enabled);


#ifdef __cplusplus
}
#endif
#endif //ZITI_SDK_ZITI_LOG_H
