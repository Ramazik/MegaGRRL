#include "freertos/FreeRTOS.h"
#include "channels.h"
#include "leddrv.h"
#include "freertos/task.h"
#include "i2c.h"
#include "esp_log.h"

static const char* TAG = "ChannelMgr";

volatile uint8_t ChannelMgr_States[6+4];
ChannelLedState_t ChannelMgr_LedStates[6+4];
uint32_t ChannelMgr_BrTime[6+4];
uint8_t ChannelMgr_States_Old[6+4];
volatile uint32_t ChannelMgr_PcmAccu;
volatile uint32_t ChannelMgr_PcmCount;

void ChannelMgr_Reset() {
    for (uint8_t i=0;i<6+4;i++) {
        ChannelMgr_States[i] = 0;
        ChannelMgr_States_Old[i] = 0;
    }
}

bool ChannelMgr_Setup() {
    ESP_LOGI(TAG, "Setting up !!");
    ChannelMgr_Reset();
    return true;
}

void ChannelMgr_Main() {
    while (1) {
        for (uint8_t i=0;i<6+4;i++) { //fm, psg
            if (ChannelMgr_LedStates[i] == LEDSTATE_BRIGHT && xthal_get_ccount() - ChannelMgr_BrTime[i] >= 50*240000) {
                ChannelMgr_LedStates[i] = LEDSTATE_ON;
            }
            if ((ChannelMgr_States_Old[i] & CHSTATE_KON) == 0 && (ChannelMgr_States[i] & CHSTATE_KON)) { //kon rising edge
                ChannelMgr_BrTime[i] = xthal_get_ccount();
                ChannelMgr_LedStates[i] = LEDSTATE_BRIGHT;
            } else if ((ChannelMgr_States[i] & CHSTATE_PARAM) && (ChannelMgr_States[i] & CHSTATE_KON)) { //param rising edge
                ChannelMgr_BrTime[i] = xthal_get_ccount();
                ChannelMgr_LedStates[i] = LEDSTATE_BRIGHT;
            } else if ((ChannelMgr_States[i] & CHSTATE_KON) == 0) {
                ChannelMgr_LedStates[i] = LEDSTATE_OFF;
            }

            //dac override
            if (i == 5) {
                if (ChannelMgr_States[5] & CHSTATE_DAC) {
                    ChannelMgr_LedStates[5] = LEDSTATE_ON;
                }
            }

            ChannelMgr_States[i] &= ~CHSTATE_PARAM;
            
            switch (ChannelMgr_LedStates[i]) {
                case LEDSTATE_BRIGHT:
                LedDrv_States[i] = 255;
                    break;
                case LEDSTATE_ON:
                LedDrv_States[i] = 96;
                    break;
                case LEDSTATE_OFF:
                LedDrv_States[i] = 0;
                    break;
                default:
                    break;
            }

            ChannelMgr_States_Old[i] = ChannelMgr_States[i];
        }
        uint32_t avg = 0;
        if (ChannelMgr_PcmCount) {
            avg = ChannelMgr_PcmAccu / ChannelMgr_PcmCount;
            ChannelMgr_PcmAccu = ChannelMgr_PcmCount = 0;
            avg <<= 1;
            if (avg > 255) avg = 255;
        }
        LedDrv_States[6+4] = avg;
        if (!I2cMgr_Seize(false, pdMS_TO_TICKS(1000))) {
            ESP_LOGE(TAG, "Couldn't seize bus !!");
            return false;
        }
        LedDrv_Update();
        I2cMgr_Release(false);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
