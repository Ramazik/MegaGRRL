#include "freertos/FreeRTOS.h"
#include "userled.h"
#include "leddrv.h"
#include "freertos/task.h"
#include "i2c.h"
#include "esp_log.h"
#include "taskmgr.h"
#include <string.h>

static const char* TAG = "UserLed";

volatile uint8_t UserLedMgr_States[3];
volatile uint8_t UserLedMgr_DiskState[DISKSTATE_COUNT];

void UserLedMgr_Reset() {
    memset(&UserLedMgr_States[0], 0, sizeof(UserLedMgr_States));
}

bool UserLedMgr_Setup() {
    ESP_LOGI(TAG, "Setting up !!");
    UserLedMgr_Reset();
    return true;
}

void UserLedMgr_Main() {
    while (1) {
        xTaskNotifyWait(0,0xffffffff, NULL, pdMS_TO_TICKS(500));
        //memcpy(&LedDrv_States[7+4], &UserLedMgr_States[0], 3);

        //todo: actually let user specify stuff. maybe,
        bool set = false;
        for (uint8_t i=0;i<DISKSTATE_COUNT;i++) {
            if (UserLedMgr_DiskState[i]) {
                set = true;
                break;
            }
        }
        LedDrv_States[7+4] = set?255:0;

        if (!I2cMgr_Seize(false, pdMS_TO_TICKS(1000))) {
            ESP_LOGE(TAG, "Couldn't seize bus !!");
            return false;
        }
        LedDrv_Update();
        I2cMgr_Release(false);
    }
}

void UserLedMgr_Notify() {
    xTaskNotify(Taskmgr_Handles[TASK_USERLED], 1, eIncrement);
}