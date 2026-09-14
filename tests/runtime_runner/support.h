#ifndef RUNTIME_RUNNER_TEST_SUPPORT_H
#define RUNTIME_RUNNER_TEST_SUPPORT_H
#include "paicore_runner.h"
#include <stdio.h>
#define CHECK(condition, label)                                                \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", label);                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)
#ifdef __cplusplus
extern "C" {
#endif
int host_build_artifact(uint8_t *buf, unsigned capacity, unsigned *size_out,
                        uint32_t thread_id, uint32_t output_base,
                        uint32_t layout, uint32_t pipeline_latency,
                        bool voltage, uint32_t timesteps);
int host_build_data_artifact(uint8_t *buf, unsigned capacity,
                             unsigned *size_out, uint32_t thread_id,
                             uint32_t output_base, uint32_t layout,
                             uint32_t pipeline_latency);
typedef uint32_t (*host_mock_gen_fn)(void *, uint32_t, uint32_t, rvrt_frame_t *,
                                     uint32_t);
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);
uint32_t host_mock_sent_count(void);
#ifdef __cplusplus
}
#endif
#endif
