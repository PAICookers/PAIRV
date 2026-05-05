#ifndef _RV_PAICORE_NOC_H
#define _RV_PAICORE_NOC_H

#include "nuclei_sdk_soc.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rv_paicore_noc_frame {
    /* Logical frame bits [63:32] after `noc_fifo_read_frame_words()`. */
    uint32_t high;
    /* Logical frame bits [31:0] after `noc_fifo_read_frame_words()`. */
    uint32_t low;
    /* 1-based frame index within the current IRQ drain pass. */
    uint32_t index;
} rv_paicore_noc_frame_t;

typedef struct rv_paicore_noc_irq_state {
    uint32_t frame_count;
    uint32_t finish_count;
    bool stop;
    bool data_ready;
} rv_paicore_noc_irq_state_t;

/**
 * Per-frame callback used by the shared PAICORE/NoC IRQ drain path.
 *
 * @param frame
 * One legal uplink frame already split into logical `high32`/`low32` halves.
 * This is a frame-semantics view, not a statement about how any local
 * `uint32_t` array may have stored adjacent words before sending.
 * @param state
 * Mutable drain-pass state. Callbacks may request stop or publish readiness.
 * @param ctx
 * Opaque application-owned context pointer installed with
 * `rv_paicore_noc_set_frame_handler()`.
 *
 * The callback may update the mutable IRQ state to stop draining, count finish
 * markers, or publish that a batch of received data is ready for later
 * processing in thread context.
 */
typedef void (*rv_paicore_noc_frame_fn)(const rv_paicore_noc_frame_t *frame,
                                        rv_paicore_noc_irq_state_t *state,
                                        void *ctx);

/**
 * Install or replace the frame callback used by the default shared IRQ
 * handler.
 *
 * This does not register the interrupt with ECLIC; applications still own that
 * policy.
 */
void rv_paicore_noc_set_frame_handler(rv_paicore_noc_frame_fn handler,
                                      void *ctx);

/** Clear the installed callback and its opaque context pointer. */
void rv_paicore_noc_reset_handler(void);

/**
 * Register the default shared PAICORE/NoC interrupt entry with ECLIC.
 *
 * The library never self-registers during startup; applications must opt in
 * explicitly and provide the desired trigger/priority policy.
 */
int32_t rv_paicore_noc_register_irq(uint8_t shv, ECLIC_TRIGGER_Type trig_mode,
                                    uint8_t lvl, uint8_t priority);

/**
 * Default weak PAICORE/NoC interrupt entry.
 *
 * Applications may override this symbol entirely, or keep it and only install
 * a frame callback through rv_paicore_noc_set_frame_handler().
 */
void paicore_noc_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _RV_PAICORE_NOC_H */
