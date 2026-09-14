#include "support.h"
#include <string.h>

static uint8_t artifact[4096] __attribute__((aligned(16)));
static unsigned artifact_size;
static rvrt_frame_t scratch[8];
static rvrt_voltage_decode_state_t voltage_state[4];

static rvrt_paicore_runner_prepare_config_t prepare_config(void)
{
    return (rvrt_paicore_runner_prepare_config_t){
        .artifact_data = artifact,
        .artifact_size = artifact_size,
        .voltage_state = voltage_state,
        .voltage_state_capacity = 4U,
        .timeout_ms = 100U,
    };
}
static rvrt_paicore_runner_attach_config_t attach_config(void)
{
    return (rvrt_paicore_runner_attach_config_t){
        .frame_buffer = scratch,
        .frame_capacity = 8U,
    };
}
static int transitions(void)
{
    rvrt_paicore_runner_t runners[5] = {0};
    const rvrt_paicore_runner_prepare_config_t prepare = prepare_config();
    const rvrt_paicore_runner_attach_config_t attach = attach_config();
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY cannot attach");
    CHECK(rvrt_paicore_runner_load_config(&runners[0]) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY cannot load");
    CHECK(rvrt_paicore_runner_detach(&runners[0]) == RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY cannot detach");
    for (unsigned i = 0; i < 5; ++i) {
        CHECK(rvrt_paicore_runner_prepare(&runners[i], &prepare) ==
                  RVRT_SESSION_OK,
              "multiple prepares do not acquire IRQ");
    }
    CHECK(rvrt_paicore_runner_prepare(&runners[0], &prepare) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "PREPARED cannot prepare again");
    CHECK(rvrt_paicore_runner_load_config(&runners[0]) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "PREPARED cannot load");
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) == RVRT_SESSION_OK,
          "attach first runner");
    CHECK(rvrt_paicore_runner_attach(&runners[1], &attach) == RVRT_SESSION_BUSY,
          "only one attached runner owns IRQ");
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "ATTACHED cannot attach twice");
    host_mock_set_generator(NULL, NULL);
    CHECK(rvrt_paicore_runner_load_config(&runners[0]) == RVRT_SESSION_OK,
          "load configuration once");
    const uint32_t sent = host_mock_sent_count();
    CHECK(rvrt_paicore_runner_load_config(&runners[0]) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "second load rejected");
    CHECK(rvrt_paicore_runner_detach(&runners[0]) == RVRT_SESSION_OK,
          "detach preserves artifact/configuration");
    CHECK(rvrt_paicore_runner_attach(&runners[1], &attach) == RVRT_SESSION_OK,
          "another prepared runner can acquire IRQ");
    CHECK(rvrt_paicore_runner_detach(&runners[1]) == RVRT_SESSION_OK,
          "detach second runner");
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) == RVRT_SESSION_OK,
          "reattach loaded runner");
    uint8_t input[1] = {0};
    uint8_t output[4] = {1, 1, 1, 1};
    CHECK(rvrt_paicore_runner_run_sample(&runners[0], input, 1U, 1U, output, 4U,
                                         4U) == RVRT_SESSION_OK,
          "sparse sample succeeds after reattach");
    CHECK(host_mock_sent_count() == sent + 2U,
          "reattach does not reload configuration");
    CHECK(output[0] == 0U && output[1] == 0U && output[2] == 0U &&
              output[3] == 0U,
          "omitted sparse DATA values remain zero");
    for (unsigned i = 0; i < 5; ++i) {
        CHECK(rvrt_paicore_runner_release(&runners[i]) == RVRT_SESSION_OK,
              "release runner");
    }
    CHECK(rvrt_paicore_runner_release(&runners[0]) == RVRT_SESSION_OK,
          "release is idempotent");
    return 0;
}
static int failures(void)
{
    rvrt_paicore_runner_t runner = {0};
    rvrt_paicore_runner_prepare_config_t prepare = prepare_config();
    rvrt_paicore_runner_attach_config_t attach = attach_config();
    prepare.artifact_size = 1U;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "malformed artifact returns failure");
    CHECK(runner.state == RVRT_PAICORE_RUNNER_EMPTY &&
              runner.artifact.root == NULL,
          "prepare failure clears ownership");
    prepare = prepare_config();
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare after failure");
    attach.frame_capacity = 0U;
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "invalid scratch rejected without acquiring IRQ");
    attach = attach_config();
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach after bad scratch");
    /* Inject a lower-level load failure to exercise sticky runner ownership. */
    runner.session.faulted = true;
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_FAULTED,
          "load error is propagated");
    CHECK(runner.config_load_failed && !runner.config_loaded,
          "failed load is sticky");
    CHECK(rvrt_paicore_runner_load_config(&runner) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "failed load cannot retry");
    CHECK(rvrt_paicore_runner_detach(&runner) == RVRT_SESSION_OK,
          "failed runner can detach");
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "failed runner cannot reattach");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "failed runner can release");
    rvrt_paicore_runner_deploy_config_t deploy = {
        .artifact_data = artifact,
        .artifact_size = artifact_size,
        .frame_buffer = scratch,
        .frame_capacity = 0U,
        .timeout_ms = 100U,
    };
    CHECK(rvrt_paicore_runner_deploy(&runner, &deploy) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "deploy propagates attach failure");
    CHECK(runner.state == RVRT_PAICORE_RUNNER_EMPTY,
          "failed deploy releases prepared state");
    deploy.frame_capacity = 8U;
    CHECK(rvrt_paicore_runner_deploy(&runner, &deploy) == RVRT_SESSION_OK,
          "deploy wrapper succeeds");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release deployed runner");
    CHECK(host_build_artifact(artifact, sizeof(artifact), &artifact_size, 0U,
                              32U, 0U, 1U, true, 1U) == 0,
          "build voltage fixture");
    prepare = prepare_config();
    prepare.voltage_state_capacity = 3U;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) ==
              RVRT_SESSION_BUFFER_TOO_SMALL,
          "short voltage state has precise error");
    CHECK(runner.state == RVRT_PAICORE_RUNNER_EMPTY,
          "short voltage state leaves EMPTY");
    return 0;
}
int main(void)
{
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   0U, 32U, 0U, 1U) == 0,
          "build synthetic fixture");
    CHECK(transitions() == 0, "lifecycle transitions");
    CHECK(failures() == 0, "lifecycle failures");
    puts("RUNTIME LIFECYCLE: PASS");
    return 0;
}
