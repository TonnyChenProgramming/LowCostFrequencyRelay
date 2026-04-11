#ifndef INTERRUPT_H
#define INTERRUPT_H
/* Standard includes. */
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "system.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


void ISR_Init(void);
void FreqAnalyserISR(void *context);
void button_interrupts_function(void* context, alt_u32 id);
#endif
