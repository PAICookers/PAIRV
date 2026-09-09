/*
 * Fixed-size profiling state for one complete SNN Head invocation.
 * The implementation is compiled out when SNN_HEAD_TIMING=0; enabled builds
 * retain only the latest call and require no allocation or per-frame logging.
 */
#include "snn_head_profile.h"

#if SNN_HEAD_TIMING

#include <string.h>

#include "nuclei_sdk_soc.h"

static snn_head_profile_t snn_head_profile;
static snn_head_layer_id_t active_layer = SNN_HEAD_LAYER_COUNT;
static snn_head_profile_phase_t active_phase;
static rv_counter_t layer_start;
static rv_counter_t phase_start;

static uint64_t *phase_cycles(snn_head_layer_profile_t *layer,
                              snn_head_profile_phase_t phase)
{
    switch (phase) {
        case SNN_HEAD_PROFILE_CPU_PREPROCESS:
            return &layer->cpu_preprocess_cycles;
        case SNN_HEAD_PROFILE_ARTIFACT_VALIDATE:
            return &layer->artifact_validate_cycles;
        case SNN_HEAD_PROFILE_DEPLOY:
            return &layer->deploy_cycles;
        case SNN_HEAD_PROFILE_SAMPLE:
            return &layer->sample_cycles;
        case SNN_HEAD_PROFILE_RELEASE:
            return &layer->release_cycles;
        case SNN_HEAD_PROFILE_CPU_POSTPROCESS:
            return &layer->cpu_postprocess_cycles;
        default:
            return NULL;
    }
}

void snn_head_profile_reset(void)
{
    memset(&snn_head_profile, 0, sizeof(snn_head_profile));
    active_layer = SNN_HEAD_LAYER_COUNT;
}

void snn_head_profile_layer_begin(snn_head_layer_id_t layer)
{
    if (layer >= SNN_HEAD_LAYER_COUNT) {
        return;
    }
    active_layer = layer;
    layer_start = __get_rv_cycle();
}

void snn_head_profile_layer_end(snn_head_layer_id_t layer)
{
    if ((active_layer != layer) || (layer >= SNN_HEAD_LAYER_COUNT)) {
        return;
    }
    snn_head_layer_profile_t *const entry = &snn_head_profile.layers[layer];
    entry->total_cycles = __get_rv_cycle() - layer_start;
    const uint64_t accounted =
        entry->cpu_preprocess_cycles + entry->artifact_validate_cycles +
        entry->deploy_cycles + entry->sample_cycles + entry->release_cycles +
        entry->cpu_postprocess_cycles;
    entry->residual_cycles =
        entry->total_cycles > accounted ? entry->total_cycles - accounted : 0U;
    active_layer = SNN_HEAD_LAYER_COUNT;
}

void snn_head_profile_phase_begin(snn_head_profile_phase_t phase)
{
    if ((active_layer >= SNN_HEAD_LAYER_COUNT) ||
        (phase >= SNN_HEAD_PROFILE_CPU_POSTPROCESS + 1U)) {
        return;
    }
    active_phase = phase;
    phase_start = __get_rv_cycle();
}

void snn_head_profile_phase_end(snn_head_profile_phase_t phase)
{
    if ((active_layer >= SNN_HEAD_LAYER_COUNT) || (active_phase != phase)) {
        return;
    }
    uint64_t *const destination =
        phase_cycles(&snn_head_profile.layers[active_layer], phase);
    if (destination != NULL) {
        *destination += __get_rv_cycle() - phase_start;
    }
}

void snn_head_profile_record_runner(
    const uint64_t *sync_round_trip_cycles, uint32_t sync_round_trip_count,
    uint64_t init_round_trip_cycles, uint64_t config_submit_cycles,
    uint64_t input_encode_cycles, uint64_t input_submit_cycles,
    uint64_t sync_wait_cycles, uint64_t rx_irq_service_cycles,
    uint32_t config_frames, uint32_t input_frames, uint32_t output_work_frames,
    uint32_t complete_frames, uint32_t rx_irq_count)
{
    if (active_layer >= SNN_HEAD_LAYER_COUNT) {
        return;
    }
    snn_head_layer_profile_t *const entry =
        &snn_head_profile.layers[active_layer];
    entry->init_round_trip_cycles += init_round_trip_cycles;
    entry->config_submit_cycles += config_submit_cycles;
    entry->input_encode_cycles += input_encode_cycles;
    entry->input_submit_cycles += input_submit_cycles;
    entry->sync_wait_cycles += sync_wait_cycles;
    entry->rx_irq_service_cycles += rx_irq_service_cycles;
    entry->config_frames += config_frames;
    entry->input_frames += input_frames;
    entry->output_work_frames += output_work_frames;
    entry->complete_frames += complete_frames;
    entry->rx_irq_count += rx_irq_count;
    for (uint32_t timestep = 0U;
         (timestep < sync_round_trip_count) && (timestep < SNN_HEAD_TIMESTEPS);
         ++timestep) {
        entry->sync_round_trip_steps[timestep] =
            sync_round_trip_cycles[timestep];
        entry->sync_round_trip_cycles += sync_round_trip_cycles[timestep];
    }
}

typedef struct {
    snn_head_profile_write_fn write;
    void *ctx;
    bool ok;
} report_writer_t;

static void write_bytes(report_writer_t *writer, const char *data,
                        size_t length)
{
    if (writer->ok) {
        writer->ok = writer->write(writer->ctx, data, length);
    }
}

static void write_text(report_writer_t *writer, const char *text)
{
    write_bytes(writer, text, strlen(text));
}

static void write_u64(report_writer_t *writer, uint64_t value)
{
    char digits[20U];
    size_t first = sizeof(digits);
    do {
        digits[--first] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    write_bytes(writer, digits + first, sizeof(digits) - first);
}

static void write_field(report_writer_t *writer, const char *name,
                        uint64_t value)
{
    write_text(writer, " ");
    write_text(writer, name);
    write_text(writer, "=");
    write_u64(writer, value);
}

bool snn_head_profile_write_report(snn_head_profile_write_fn write, void *ctx,
                                   uint32_t hz, uint64_t chain_cycles,
                                   const snn_head_transport_timing_t *transport)
{
    if (write == NULL) {
        return false;
    }
    report_writer_t writer = {.write = write, .ctx = ctx, .ok = true};
    if (chain_cycles == 0U) {
        for (uint32_t layer = 0U; layer < SNN_HEAD_LAYER_COUNT; ++layer) {
            chain_cycles += snn_head_profile.layers[layer].total_cycles;
        }
    }

    static const char *const names[SNN_HEAD_LAYER_COUNT] = {
        "fc1_lif", "block0", "block1", "fc2", "fc3",
    };
    write_text(&writer, "SNN_HEAD_TIMING_BEGIN");
    write_field(&writer, "hz", hz);
    write_field(&writer, "chain_cycles", chain_cycles);
    write_text(&writer, "\n");
    if (transport != NULL) {
        write_text(&writer, "SNN_HEAD_TIMING uart_rx_payload");
        write_field(&writer, "cycles", transport->input_payload_rx_cycles);
        write_text(&writer, "\nSNN_HEAD_TIMING uart_decode_float");
        write_field(&writer, "cycles", transport->input_decode_cycles);
        write_text(&writer, "\nSNN_HEAD_TIMING uart_result_encode_tx");
        write_field(&writer, "cycles", transport->result_encode_tx_cycles);
        write_text(&writer, "\n");
    }
    for (uint32_t layer = 0U; layer < SNN_HEAD_LAYER_COUNT; ++layer) {
        const snn_head_layer_profile_t *const entry =
            &snn_head_profile.layers[layer];
        if (entry->total_cycles == 0U) {
            continue;
        }
        write_text(&writer, "SNN_HEAD_TIMING layer=");
        write_text(&writer, names[layer]);
        write_field(&writer, "total_cycles", entry->total_cycles);
        write_field(&writer, "cpu_preprocess_cycles",
                    entry->cpu_preprocess_cycles);
        write_field(&writer, "artifact_validate_cycles",
                    entry->artifact_validate_cycles);
        write_field(&writer, "deploy_cycles", entry->deploy_cycles);
        write_field(&writer, "sample_cycles", entry->sample_cycles);
        write_field(&writer, "config_submit_cycles",
                    entry->config_submit_cycles);
        write_field(&writer, "input_encode_cycles", entry->input_encode_cycles);
        write_field(&writer, "input_submit_cycles", entry->input_submit_cycles);
        write_field(&writer, "init_round_trip_cycles",
                    entry->init_round_trip_cycles);
        write_field(&writer, "sync_round_trip_cycles",
                    entry->sync_round_trip_cycles);
        write_field(&writer, "release_cycles", entry->release_cycles);
        write_field(&writer, "cpu_postprocess_cycles",
                    entry->cpu_postprocess_cycles);
        write_field(&writer, "residual_cycles", entry->residual_cycles);
        write_field(&writer, "sync_wait_cycles", entry->sync_wait_cycles);
        write_field(&writer, "rx_irq_service_cycles",
                    entry->rx_irq_service_cycles);
        write_field(&writer, "config_frames", entry->config_frames);
        write_field(&writer, "input_frames", entry->input_frames);
        write_field(&writer, "output_work_frames", entry->output_work_frames);
        write_field(&writer, "complete_frames", entry->complete_frames);
        write_field(&writer, "rx_irq_count", entry->rx_irq_count);
        write_text(&writer, "\n");
        for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS;
             ++timestep) {
            write_text(&writer, "SNN_HEAD_TIMING step layer=");
            write_text(&writer, names[layer]);
            write_field(&writer, "timestep", timestep);
            write_field(&writer, "sync_round_trip_cycles",
                        entry->sync_round_trip_steps[timestep]);
            write_text(&writer, "\n");
        }
    }
    write_text(&writer, "SNN_HEAD_TIMING_END\n");
    return writer.ok;
}

#else

static const snn_head_profile_t snn_head_profile;

bool snn_head_profile_write_report(snn_head_profile_write_fn write, void *ctx,
                                   uint32_t hz, uint64_t chain_cycles,
                                   const snn_head_transport_timing_t *transport)
{
    (void)write;
    (void)ctx;
    (void)hz;
    (void)chain_cycles;
    (void)transport;
    return false;
}

#endif

const snn_head_profile_t *snn_head_profile_get(void)
{
    return &snn_head_profile;
}
