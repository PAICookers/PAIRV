#ifndef TEST_EVALSOC_NOC_H
#define TEST_EVALSOC_NOC_H

#include <stdbool.h>
#include <stdint.h>

void noc_fifo_write_frame_words(uint32_t high, uint32_t low);
void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low);
int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low);

uint32_t noc_irq_pending(void);
bool noc_irq_is_enabled(void);
void noc_irq_ack(void);
void noc_irq_enable(void);
void noc_irq_disable(void);

#endif /* TEST_EVALSOC_NOC_H */
