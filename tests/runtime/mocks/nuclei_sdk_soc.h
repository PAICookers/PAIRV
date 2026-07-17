#ifndef TEST_NUCLEI_SDK_SOC_H
#define TEST_NUCLEI_SDK_SOC_H

#include <stdint.h>

typedef uint64_t rv_counter_t;

rv_counter_t __get_rv_cycle(void);

#define __RARELY(condition) (condition)
#define __USUALLY(condition) (condition)
#define __WMB() __sync_synchronize()
#define __RMB() __sync_synchronize()
#define __enable_irq() ((void)0)
#define SystemCoreClock 1000U

#define SAVE_IRQ_CSR_CONTEXT() ((void)0)
#define RESTORE_IRQ_CSR_CONTEXT() ((void)0)

#define PAICORE_NOC_IRQn 1
#define ECLIC_NON_VECTOR_INTERRUPT 0U
#define ECLIC_LEVEL_TRIGGER 0U

int32_t ECLIC_Register_IRQ(int32_t irqn, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *handler);

#endif /* TEST_NUCLEI_SDK_SOC_H */
