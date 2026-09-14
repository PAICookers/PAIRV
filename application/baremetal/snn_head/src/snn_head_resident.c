#include "snn_head_internal.h"

#if SNN_HEAD_RESIDENT

typedef struct {
    const uint8_t *start;
    const uint8_t *size_symbol;
    snn_head_layer_artifact_context_t context;
    rvrt_paicore_runner_t runner;
} resident_layer_t;

static resident_layer_t layers[SNN_HEAD_LAYER_COUNT] = {
    {.start = snn_head_fc1_lif_artifact_start,
     .size_symbol = snn_head_fc1_lif_artifact_size},
    {.start = snn_head_block0_lif_artifact_start,
     .size_symbol = snn_head_block0_lif_artifact_size},
    {.start = snn_head_block1_lif_artifact_start,
     .size_symbol = snn_head_block1_lif_artifact_size},
    {.start = snn_head_fc2_artifact_start,
     .size_symbol = snn_head_fc2_artifact_size},
    {.start = snn_head_fc3_artifact_start,
     .size_symbol = snn_head_fc3_artifact_size},
};

/* One receiver owns this bitmap at a time: FC2 is the largest output,
 * 8 timesteps * 1536 elements * four byte lanes = 49152 bits. */
static uint32_t coverage[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM * 4U / 32U];
static snn_head_initialization_stats_t initialization;
static bool initialization_attempted;

static const rvrt_paicore_runner_attach_config_t receiver = {
    .frame_buffer = layer_frame_buf,
    .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    .coverage_bitmap = coverage,
    .coverage_word_capacity = sizeof(coverage) / sizeof(coverage[0]),
};

const snn_head_initialization_stats_t *snn_head_initialization_stats(void)
{
    return &initialization;
}

bool snn_head_initialize(void)
{
    if (initialization_attempted) {
        return initialization.ready;
    }
    initialization_attempted = true;
    const rv_counter_t start = __get_rv_cycle();
    for (unsigned i = 0; i < SNN_HEAD_LAYER_COUNT; ++i) {
        resident_layer_t *const layer = &layers[i];
        const bool voltage = i >= SNN_HEAD_LAYER_FC2;
        const uint32_t outputs =
            i == SNN_HEAD_LAYER_FC3 ? SNN_HEAD_ACTION_DIM : SNN_HEAD_HIDDEN_DIM;
        const snn_head_layer_artifact_contract_t contract = {
            i == SNN_HEAD_LAYER_FC1_LIF ? SNN_HEAD_INPUT_DIM
                                        : SNN_HEAD_HIDDEN_DIM,
            8U,
            outputs,
            outputs,
            voltage ? RVRT_OUTPUT_VOLTAGE : RVRT_OUTPUT_DATA,
            voltage ? SNN_HEAD_DTYPE_INT32 : SNN_HEAD_DTYPE_UINT1,
        };
        if (!snn_head_parse_layer_artifact(layer->start, layer->size_symbol,
                                           &layer->context) ||
            !snn_head_validate_layer_artifact(&layer->context, &contract)) {
            goto failed;
        }
        const rvrt_paicore_runner_prepare_config_t config = {
            .artifact_data = layer->start,
            .artifact_size = snn_head_artifact_size(layer->size_symbol),
            .voltage_state = voltage ? &fc2_voltage_state[0][0] : NULL,
            .voltage_state_capacity =
                voltage ? SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM : 0U,
            .timeout_ms = SNN_HEAD_TIMEOUT_MS,
            .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
        };
        if (rvrt_paicore_runner_prepare(&layer->runner, &config) !=
            RVRT_SESSION_OK) {
            goto failed;
        }
    }
    initialization.prepare_cycles = __get_rv_cycle() - start;

    const rv_counter_t load_start = __get_rv_cycle();
    for (unsigned i = 0; i < SNN_HEAD_LAYER_COUNT; ++i) {
        resident_layer_t *const layer = &layers[i];
        uint32_t words = 0U;
        if (rvrt_artifact_config_word_count(&layer->context.artifact, &words) !=
                RVRT_ARTIFACT_OK ||
            rvrt_paicore_runner_attach(&layer->runner, &receiver) !=
                RVRT_SESSION_OK) {
            goto failed;
        }
        const rvrt_session_status_t status =
            rvrt_paicore_runner_load_config(&layer->runner);
        const rvrt_session_status_t detach_status =
            rvrt_paicore_runner_detach(&layer->runner);
        if (status != RVRT_SESSION_OK || detach_status != RVRT_SESSION_OK) {
            goto failed;
        }
        initialization.config_frames += words / 2U;
    }
    initialization.load_cycles = __get_rv_cycle() - load_start;
    initialization.total_cycles = __get_rv_cycle() - start;
    initialization.ready = true;
    return true;

failed:
    initialization.total_cycles = __get_rv_cycle() - start;
    return false;
}

bool snn_head_resident_artifact(const uint8_t *start,
                                const uint8_t *size_symbol,
                                snn_head_layer_artifact_context_t *context)
{
    if (!initialization.ready || context == NULL) {
        return false;
    }
    for (unsigned i = 0; i < SNN_HEAD_LAYER_COUNT; ++i) {
        if (start == layers[i].start && size_symbol == layers[i].size_symbol) {
            *context = layers[i].context;
            return true;
        }
    }
    return false;
}

rvrt_paicore_runner_t *snn_head_resident_acquire(const uint8_t *start,
                                                 size_t size)
{
    if (!initialization.ready) {
        return NULL;
    }
    for (unsigned i = 0; i < SNN_HEAD_LAYER_COUNT; ++i) {
        if (start == layers[i].start &&
            size == snn_head_artifact_size(layers[i].size_symbol)) {
            if (rvrt_paicore_runner_attach(&layers[i].runner, &receiver) ==
                RVRT_SESSION_OK) {
                return &layers[i].runner;
            }
            break;
        }
    }
    initialization.ready = false;
    return NULL;
}

bool snn_head_resident_finish(rvrt_paicore_runner_t *runner, bool success)
{
    const rvrt_session_status_t status = rvrt_paicore_runner_detach(runner);
    if (!success || status != RVRT_SESSION_OK) {
        initialization.ready = false;
        return false;
    }
    return true;
}
#endif
