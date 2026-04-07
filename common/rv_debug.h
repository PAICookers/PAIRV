#ifndef RV_DEBUG_H
#define RV_DEBUG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rv_debug_level_e {
    RV_DEBUG_OFF = 0,
    RV_DEBUG_ERROR = 1,
    RV_DEBUG_WARN = 2,
    RV_DEBUG_INFO = 3,
    RV_DEBUG_DEBUG = 4,
} rv_debug_level_t;

typedef void (*rv_debug_sink_t)(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *message,
    void *user_data);

void rv_debug_set_level(rv_debug_level_t level);
rv_debug_level_t rv_debug_get_level(void);
void rv_debug_set_sink(rv_debug_sink_t sink, void *user_data);

void rv_debug_vlogf(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *fmt,
    va_list ap);

void rv_debug_logf(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *fmt,
    ...);

#ifndef RV_DEBUG_COMPILETIME_ENABLE
#define RV_DEBUG_COMPILETIME_ENABLE 1
#endif

#if RV_DEBUG_COMPILETIME_ENABLE
#define RV_DEBUG_LOGE(title, fmt, ...)                                          \
    rv_debug_logf(                                                              \
        RV_DEBUG_ERROR, title, __func__, fmt, ##__VA_ARGS__)
#define RV_DEBUG_LOGW(title, fmt, ...)                                          \
    rv_debug_logf(                                                              \
        RV_DEBUG_WARN, title, __func__, fmt, ##__VA_ARGS__)
#define RV_DEBUG_LOGI(title, fmt, ...)                                          \
    rv_debug_logf(                                                              \
        RV_DEBUG_INFO, title, __func__, fmt, ##__VA_ARGS__)
#define RV_DEBUG_LOGD(title, fmt, ...)                                          \
    rv_debug_logf(                                                              \
        RV_DEBUG_DEBUG, title, __func__, fmt, ##__VA_ARGS__)
#else
#define RV_DEBUG_LOGE(title, fmt, ...) ((void)0)
#define RV_DEBUG_LOGW(title, fmt, ...) ((void)0)
#define RV_DEBUG_LOGI(title, fmt, ...) ((void)0)
#define RV_DEBUG_LOGD(title, fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* RV_DEBUG_H */
