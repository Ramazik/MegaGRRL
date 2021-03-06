#include "freertos/FreeRTOS.h"
#include "statusbar.h"
#include "esp_log.h"
#include "lvgl.h"
#include "../lcddma.h"

static const char* TAG = "Ui_SoftBar";

lv_obj_t *container;
lv_obj_t *softbarlabels[3];
lv_style_t softbarstyle;
lv_style_t softbarstyle_disbl;

bool Ui_SoftBar_Setup(lv_obj_t *uiscreen) {
    LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));
    
    container = lv_cont_create(uiscreen, NULL);

    lv_style_copy(&softbarstyle, &lv_style_plain);
    softbarstyle.body.main_color = LV_COLOR_MAKE(0,0,127);
    softbarstyle.body.grad_color = LV_COLOR_MAKE(0,0,64);
    softbarstyle.body.radius = 0;
    softbarstyle.line.color = LV_COLOR_MAKE(255,255,255);
    softbarstyle.line.width = 2;
    softbarstyle.text.font = &lv_font_dejavu_20;
    softbarstyle.text.color = LV_COLOR_MAKE(220,220,220);
    lv_style_copy(&softbarstyle_disbl, &softbarstyle);
    softbarstyle_disbl.text.color = LV_COLOR_MAKE(100,100,100);

    lv_cont_set_style(container, &softbarstyle);
    lv_obj_set_height(container, 34);
    lv_obj_set_width(container, 240);
    lv_obj_set_pos(container, 0, 320-34);
    lv_cont_set_fit(container, false, false);
    
    static lv_point_t points[] = {{0,0}, {0,34}};
    for (uint8_t i=1;i<=2;i++) {
        lv_obj_t *line = lv_line_create(container, NULL);
        lv_line_set_points(line, points, 2);
        lv_obj_set_pos(line, ((240/3)*i)-1, 0);
        lv_line_set_style(line, &softbarstyle);
    }

    for (uint8_t i=0;i<3;i++) {
        softbarlabels[i] = lv_label_create(container, NULL);
        lv_obj_set_style(softbarlabels[i], &softbarstyle);
        lv_label_set_long_mode(softbarlabels[i], LV_LABEL_LONG_EXPAND);
        lv_label_set_align(softbarlabels[i], LV_LABEL_ALIGN_LEFT);
        lv_label_set_text(softbarlabels[i], "");
        lv_obj_set_pos(softbarlabels[i], ((240/3)*i)+3, 6);
    }

    LcdDma_Mutex_Give();

    return true;
}

bool Ui_SoftBar_Update(uint8_t id, bool enabled, char *text, bool HandleMutex) {
    if (HandleMutex) LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));
    lv_obj_set_style(softbarlabels[id], enabled?&softbarstyle:&softbarstyle_disbl);
    lv_label_set_text(softbarlabels[id], text);
    if (HandleMutex) LcdDma_Mutex_Give();
    
    return true;
}