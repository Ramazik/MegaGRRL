#ifndef AGR_UI_SOFTBAR_H
#define AGR_UI_SOFTBAR_H

#include "esp_system.h"
#include "lvgl.h"

bool Ui_SoftBar_Setup(lv_obj_t *uiscreen);
bool Ui_SoftBar_Update(uint8_t id, bool enabled, char *text, bool HandleMutex);

#endif