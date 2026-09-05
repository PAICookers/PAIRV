#ifndef RV_DEBUG_H
#define RV_DEBUG_H

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

typedef void (*rv_debug_sink_t)(rv_debug_level_t level, const char *title,
                                const char *function_name, const char *message,
                                void *user_data);

#if defined(__GNUC__) || defined(__clang__)
#define RV_DEBUG_PRINTF_LIKE(format_index, first_argument)                     \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define RV_DEBUG_PRINTF_LIKE(format_index, first_argument)
#endif

void rv_debug_set_level(rv_debug_level_t level);
void rv_debug_set_sink(rv_debug_sink_t sink, void *user_data);

void rv_debug_logf(rv_debug_level_t level, const char *title,
                   const char *function_name, const char *fmt, ...)
    RV_DEBUG_PRINTF_LIKE(4, 5);

#ifndef RV_DEBUG_ENABLE_LOGGING
#define RV_DEBUG_ENABLE_LOGGING 0
#endif

#if (RV_DEBUG_ENABLE_LOGGING != 0) && (RV_DEBUG_ENABLE_LOGGING != 1)
#error "RV_DEBUG_ENABLE_LOGGING must be 0 or 1"
#endif

#if RV_DEBUG_ENABLE_LOGGING
#define RV_DEBUG_LOGE(title, ...)                                              \
    rv_debug_logf(RV_DEBUG_ERROR, title, __func__, __VA_ARGS__)
#define RV_DEBUG_LOGW(title, ...)                                              \
    rv_debug_logf(RV_DEBUG_WARN, title, __func__, __VA_ARGS__)
#define RV_DEBUG_LOGI(title, ...)                                              \
    rv_debug_logf(RV_DEBUG_INFO, title, __func__, __VA_ARGS__)
#define RV_DEBUG_LOGD(title, ...)                                              \
    rv_debug_logf(RV_DEBUG_DEBUG, title, __func__, __VA_ARGS__)
#else
#define RV_DEBUG_LOGE(...) ((void)0)
#define RV_DEBUG_LOGW(...) ((void)0)
#define RV_DEBUG_LOGI(...) ((void)0)
#define RV_DEBUG_LOGD(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* RV_DEBUG_H */
