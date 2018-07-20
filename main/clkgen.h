#ifndef AGR_CLKGEN_H
#define AGR_CLKGEN_H

#include "freertos/FreeRTOS.h"

#define CLKGEN_PSG  1
#define CLKGEN_FM   0

bool Clkgen_Setup();
bool Clkgen_SetFrequency(uint8_t Clkgen, uint32_t Frequency);

#endif