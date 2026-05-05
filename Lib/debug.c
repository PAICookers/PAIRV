#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

#define RV_DEBUG_MSG_MAX 192

static rv_debug_level_t g_rv_debug_level = RV_DEBUG_OFF;
static rv_debug_sink_t g_rv_debug_sink = NULL;
static void *g_rv_debug_user_data = NULL;

static const char *rv_debug_level_name(rv_debug_level_t level)
{
    switch (level)
    {
    case RV_DEBUG_ERROR:
        return "E";
    case RV_DEBUG_WARN:
        return "W";
    case RV_DEBUG_INFO:
        return "I";
    case RV_DEBUG_DEBUG:
        return "D";
    case RV_DEBUG_OFF:
    default:
        return "O";
    }
}

static void rv_debug_default_sink(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *message,
    void *user_data)
{
    (void)user_data;
    printf("[%s][%s] %s: %s\r\n",
           title ? title : "debug",
           rv_debug_level_name(level),
           function_name ? function_name : "-",
           message ? message : "");
}

void rv_debug_set_level(rv_debug_level_t level)
{
    g_rv_debug_level = level;
}

rv_debug_level_t rv_debug_get_level(void)
{
    return g_rv_debug_level;
}

void rv_debug_set_sink(rv_debug_sink_t sink, void *user_data)
{
    g_rv_debug_sink = sink;
    g_rv_debug_user_data = user_data;
}

void rv_debug_vlogf(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *fmt,
    va_list ap)
{
    if (level == RV_DEBUG_OFF || level > g_rv_debug_level)
    {
        return;
    }

    char message[RV_DEBUG_MSG_MAX];
    vsnprintf(message, sizeof(message), fmt, ap);

    if (g_rv_debug_sink != NULL)
    {
        g_rv_debug_sink(
            level, title, function_name, message, g_rv_debug_user_data);
        return;
    }

    rv_debug_default_sink(
        level, title, function_name, message, g_rv_debug_user_data);
}

void rv_debug_logf(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *fmt,
    ...)
{
    va_list ap;
    va_start(ap, fmt);
    rv_debug_vlogf(level, title, function_name, fmt, ap);
    va_end(ap);
}
