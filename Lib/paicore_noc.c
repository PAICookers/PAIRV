#include "paicore_noc.h"
#include <stddef.h>

static rv_paicore_noc_frame_fn rv_paicore_noc_handler = NULL;
static void *rv_paicore_noc_handler_ctx = NULL;

/*
 * Replace the callback used by the default shared NoC IRQ path. The library
 * keeps registration policy separate from callback installation.
 */
void rv_paicore_noc_set_frame_handler(rv_paicore_noc_frame_fn handler,
                                      void *ctx)
{
    rv_paicore_noc_handler = handler;
    rv_paicore_noc_handler_ctx = ctx;
}

/* Clear any installed callback so the weak default handler only drains/acks. */
void rv_paicore_noc_reset_handler(void)
{
    rv_paicore_noc_handler = NULL;
    rv_paicore_noc_handler_ctx = NULL;
}

/* Register the shared PAICORE/NoC IRQ entry while leaving IRQ policy to the
 * application. */
int32_t rv_paicore_noc_register_irq(uint8_t shv, ECLIC_TRIGGER_Type trig_mode,
                                    uint8_t lvl, uint8_t priority)
{
    return ECLIC_Register_IRQ(PAICORE_NOC_IRQn, shv, trig_mode, lvl, priority,
                              paicore_noc_handler);
}

/*
 * Default weak PAICORE/NoC interrupt entry.
 *
 * This path owns the board-specific IRQ acknowledge and masking sequence, then
 * drains received NoC frames into the installed per-frame callback until that
 * callback asks to stop. Applications may override this symbol entirely when
 * they need a different interrupt policy.
 */
__WEAK void paicore_noc_handler(void)
{
    rv_paicore_noc_irq_state_t state = {0};

    SAVE_IRQ_CSR_CONTEXT();
    noc_irq_ack();
    noc_irq_disable();

    if (rv_paicore_noc_handler != NULL) {
        while (!state.stop != 0U) {
            rv_paicore_noc_frame_t frame;
            if (noc_fifo_read_frame_words(&frame.high, &frame.low) != 0) {
                break;
            }

            state.frame_count++;
            frame.index = state.frame_count;
            rv_paicore_noc_handler(&frame, &state, rv_paicore_noc_handler_ctx);
        }
    }

    RESTORE_IRQ_CSR_CONTEXT();
    noc_irq_enable();
}
