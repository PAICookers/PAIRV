#include <stdio.h>

#include "nuclei_sdk_soc.h"
#include "rv_debug.h"

#define APP_TITLE "debug_demo"

static void uart_style_sink(
    rv_debug_level_t level,
    const char *title,
    const char *function_name,
    const char *message,
    void *user_data)
{
    const char *sink_name = (const char *)user_data;
    const char *level_name = "?";

    switch (level)
    {
    case RV_DEBUG_ERROR:
        level_name = "E";
        break;
    case RV_DEBUG_WARN:
        level_name = "W";
        break;
    case RV_DEBUG_INFO:
        level_name = "I";
        break;
    case RV_DEBUG_DEBUG:
        level_name = "D";
        break;
    case RV_DEBUG_OFF:
    default:
        level_name = "O";
        break;
    }

    printf("[%s][%s][%s] %s: %s\r\n",
           sink_name ? sink_name : "custom_sink",
           title ? title : "debug",
           level_name,
           function_name ? function_name : "-",
           message ? message : "");
}

int main(void)
{
    const unsigned long hartid = __get_hart_id();
    const unsigned long clusterid = __get_cluster_id();

    printf("%s on cluster %lu hart %lu\r\n", APP_TITLE, clusterid, hartid);
    printf("Phase 1: default sink + INFO level\r\n");

    rv_debug_set_level(RV_DEBUG_INFO);
    rv_debug_set_sink(NULL, NULL);

    RV_DEBUG_LOGE(APP_TITLE, "example error message");
    RV_DEBUG_LOGW(APP_TITLE, "example warning message");
    RV_DEBUG_LOGI(APP_TITLE, "info log is enabled at INFO level");
    RV_DEBUG_LOGD(APP_TITLE, "this debug log is hidden at DEBUG level");

    printf("Phase 2: default sink + DEBUG level\r\n");

    rv_debug_set_level(RV_DEBUG_DEBUG);

    RV_DEBUG_LOGD(APP_TITLE, "debug log is now visible");

    printf("Phase 3: custom sink + DEBUG level\r\n");

    rv_debug_set_sink(uart_style_sink, (void *)"debug_sink");

    RV_DEBUG_LOGI(APP_TITLE, "custom sink installed");

    printf("Phase 4: logging disabled\r\n");

    rv_debug_set_level(RV_DEBUG_OFF);

    RV_DEBUG_LOGE(APP_TITLE, "this should not appear");
    RV_DEBUG_LOGI(APP_TITLE, "this should not appear");

    printf("debug_demo complete\r\n");
    return 0;
}
