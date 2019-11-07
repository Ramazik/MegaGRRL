#include "muting.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "../lcddma.h"
#include "../ui.h"
#include "softbar.h"
#include "../driver.h"

lv_obj_t *container;
lv_style_t containerstyle;
const char *chnames[11] = {
    "FM 1",
    "FM 2",
    "FM 3",
    "FM 4",
    "FM 5",
    "FM 6 (FM Mode)",
    "FM 6 (DAC Mode)",
    "PSG 1",
    "PSG 2",
    "PSG 3",
    "PSG Noise"
};
const char titletext[] = "Channel Muting Setup";
const char mute[] = "Mute";
const char unmute[] = "Unmute";
const char muted[] = SYMBOL_MUTE;
const char unmuted[] = SYMBOL_VOLUME_MAX;

lv_obj_t *ch_label[11];
lv_obj_t *ch_status[11];
lv_obj_t *done_label;
lv_style_t ch_label_style;
lv_style_t ch_label_style_sel;
lv_style_t ch_on;
lv_style_t ch_off;
uint8_t ch_sel = 0;
lv_obj_t *title;
lv_style_t title_style;

void Ui_Muting_Destroy() {
    LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));
    lv_obj_del(container);
    LcdDma_Mutex_Give();
}

bool ch_en(uint8_t ch_sel) {
    if (ch_sel <= 6) {
        return (Driver_FmMask & (1<<ch_sel)) > 0;
    } else {
        return (Driver_PsgMask & (1<<(ch_sel-7))) > 0;
    }
}

void ch_set(uint8_t ch_sel, bool en) {
    if (ch_sel <= 6) {
        Driver_FmMask = ((Driver_FmMask&~(1<<ch_sel)) | ((en?1:0)<<ch_sel));
    } else {
        ch_sel -= 7;
        Driver_PsgMask = ((Driver_PsgMask&~(1<<ch_sel)) | ((en?1:0)<<ch_sel));
    }
}

void drawlist() {
    LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));

    for (uint8_t i=0;i<11;i++) {
        lv_label_set_style(ch_label[i], (ch_sel==i)?&ch_label_style_sel:&ch_label_style);
        lv_label_set_style(ch_status[i], ch_en(i)?&ch_on:&ch_off);
        lv_label_set_static_text(ch_status[i], ch_en(i)?unmuted:muted);
    }
    
    Ui_SoftBar_Update(2, true, ch_en(ch_sel)?mute:unmute, false);

    LcdDma_Mutex_Give();
}

void Ui_Muting_Setup(lv_obj_t *uiscreen) {
    LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));

    container = lv_cont_create(uiscreen, NULL);
    lv_style_copy(&containerstyle, &lv_style_plain);
    containerstyle.body.main_color = LV_COLOR_MAKE(0, 0, 0);
    containerstyle.body.grad_color = LV_COLOR_MAKE(0,0,0);
    lv_cont_set_style(container, &containerstyle);
    lv_obj_set_height(container, 250);
    lv_obj_set_width(container, 240);
    lv_obj_set_pos(container, 0, 34+1);
    lv_cont_set_fit(container, false, false);

    lv_style_copy(&ch_label_style, &lv_style_plain);
    ch_label_style.text.color = LV_COLOR_MAKE(200,200,200);
    ch_label_style.text.font = &lv_font_dejavu_20;
    lv_style_copy(&ch_label_style_sel, &ch_label_style);
    ch_label_style_sel.text.color = LV_COLOR_MAKE(255,255,0);
    lv_style_copy(&title_style, &ch_label_style);
    title_style.text.color = LV_COLOR_MAKE(255,255,255);
    lv_style_copy(&ch_on, &ch_label_style);
    ch_on.text.color = LV_COLOR_MAKE(0,255,0);
    lv_style_copy(&ch_off, &ch_label_style);
    ch_off.text.color = LV_COLOR_MAKE(255,0,0);

    //ch_sel = 0;

    for (uint8_t i=0;i<11;i++) {
        ch_label[i] = lv_label_create(container, NULL);
        lv_obj_set_pos(ch_label[i], 42, 25+(20*i));
        lv_label_set_static_text(ch_label[i], chnames[i]);

        ch_status[i] = lv_label_create(container, NULL);
        lv_obj_set_pos(ch_status[i], 20, 24+(20*i));
    }

    title = lv_label_create(container, NULL);
    lv_label_set_style(title, &title_style);
    lv_label_set_static_text(title, titletext);
    lv_obj_set_pos(title, 10, 5);

    Ui_SoftBar_Update(0, true, SYMBOL_HOME"Home", false);
    Ui_SoftBar_Update(1, true, SYMBOL_AUDIO"Player", false);

    LcdDma_Mutex_Give();

    drawlist();
}

void Ui_Muting_Key(KeyEvent_t event) {
    if (event.State & KEY_EVENT_PRESS) {
        switch (event.Key) {
            case KEY_A:
                Ui_Screen = UISCREEN_MAINMENU;
                break;
            case KEY_B:
                Ui_Screen = UISCREEN_NOWPLAYING;
                break;
            case KEY_UP:
                if (ch_sel) {
                    ch_sel--;
                    drawlist();
                }
                break;
            case KEY_DOWN:
                if (ch_sel < 10) {
                    ch_sel++;
                    drawlist();
                }
                break;
            case KEY_C:
                ch_set(ch_sel, !ch_en(ch_sel));
                if (xEventGroupGetBits(Driver_CommandEvents) & DRIVER_EVENT_RUNNING) xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_UPDATE_MUTING);
                drawlist();
                break;
        };
    }
}