#include "uart_frame.h"

#if defined(__has_include)
#if __has_include("config_frame.h")
#include "config_frame.h"
#endif
#if __has_include("init_frame.h")
#include "init_frame.h"
#endif
#if __has_include("work_frame.h")
#include "work_frame.h"
#endif
#if __has_include("sync_frame.h")
#include "sync_frame.h"
#endif
#if __has_include("test_frame.h")
#include "test_frame.h"
#endif
#endif

static frame_status_t frame_view(const uint32_t *words, size_t word_count,
                                 frame_view_t *view)
{
    if (word_count == 0U) {
        return FRAME_UNAVAILABLE;
    }
    if (words == NULL || (word_count & 1U) != 0U) {
        return FRAME_INVALID;
    }
    view->words = words;
    view->word_count = word_count;
    return FRAME_AVAILABLE;
}

frame_status_t uart_app_get_frame(frame_kind_t kind, frame_view_t *view)
{
    if (view == NULL) {
        return FRAME_INVALID;
    }
    view->words = NULL;
    view->word_count = 0U;
    switch (kind) {
        case FRAME_CONFIG:
#if defined(HAS_CONFIG_FRAME)
            return frame_view(config_frame,
                              sizeof(config_frame) / sizeof(config_frame[0]),
                              view);
#else
            return FRAME_UNAVAILABLE;
#endif
        case FRAME_INIT:
#if defined(HAS_INIT_FRAME)
            return frame_view(init_frame,
                              sizeof(init_frame) / sizeof(init_frame[0]), view);
#else
            return FRAME_UNAVAILABLE;
#endif
        case FRAME_WORK:
#if defined(HAS_WORK_FRAME)
            return frame_view(work_frame,
                              sizeof(work_frame) / sizeof(work_frame[0]), view);
#else
            return FRAME_UNAVAILABLE;
#endif
        case FRAME_SYNC:
#if defined(HAS_SYNC_FRAME)
            return frame_view(sync_frame,
                              sizeof(sync_frame) / sizeof(sync_frame[0]), view);
#else
            return FRAME_UNAVAILABLE;
#endif
        case FRAME_TEST:
#if defined(HAS_TEST_FRAME)
            return frame_view(test_frame,
                              sizeof(test_frame) / sizeof(test_frame[0]), view);
#else
            return FRAME_UNAVAILABLE;
#endif
        default:
            return FRAME_INVALID;
    }
}
