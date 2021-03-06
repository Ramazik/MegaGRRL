#include "options_cats.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "lvgl.h"
#include "../lcddma.h"
#include "../ui.h"
#include "../options.h"
#include "softbar.h"

lv_obj_t *container;
lv_style_t containerstyle;

uint8_t Options_Cat = 0;

lv_obj_t *optioncatlines[OPTION_CATEGORY_COUNT];
lv_obj_t *optioncatlabels[OPTION_CATEGORY_COUNT];
lv_style_t optioncatstyle_normal;
lv_style_t optioncatstyle_sel;

static UiScreen_t lastscreen = UISCREEN_MAINMENU;

void redrawoptcats();

void Ui_Options_Cats_Setup(lv_obj_t *uiscreen) {
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

    lv_style_copy(&optioncatstyle_normal, &lv_style_plain);
    optioncatstyle_normal.text.font = &lv_font_dejavu_20;
    optioncatstyle_normal.body.main_color = LV_COLOR_MAKE(0,0,0);
    optioncatstyle_normal.body.grad_color = LV_COLOR_MAKE(0,0,0);
    optioncatstyle_normal.text.color = LV_COLOR_MAKE(220,220,220);

    lv_style_copy(&optioncatstyle_sel, &optioncatstyle_normal);
    optioncatstyle_sel.body.main_color = LV_COLOR_MAKE(0,0,100);
    optioncatstyle_sel.body.grad_color = LV_COLOR_MAKE(0,0,100);
    optioncatstyle_sel.body.radius = 8;

    for (uint8_t i=0;i<OPTION_CATEGORY_COUNT;i++) {
        optioncatlines[i] = lv_cont_create(container, NULL);
        lv_obj_set_style(optioncatlines[i], &optioncatstyle_sel);
        lv_obj_set_height(optioncatlines[i], 25);
        lv_obj_set_width(optioncatlines[i],240);
        lv_obj_set_pos(optioncatlines[i], 0, 25*i);

        optioncatlabels[i] = lv_label_create(optioncatlines[i], NULL);
        lv_obj_set_pos(optioncatlabels[i], 2, 2);
        lv_label_set_text(optioncatlabels[i], "");
        lv_label_set_long_mode(optioncatlabels[i], (i==0xff)?LV_LABEL_LONG_ROLL:LV_LABEL_LONG_DOT);
        lv_obj_set_width(optioncatlabels[i], 240);
    }

    Ui_SoftBar_Update(0, true, SYMBOL_HOME"Home", false);
    Ui_SoftBar_Update(1, true, "Back", false);
    Ui_SoftBar_Update(2, true, "Open", false);

    LcdDma_Mutex_Give();

    redrawoptcats();

    if (Ui_Screen_Last != UISCREEN_OPTIONS_OPTS) lastscreen = Ui_Screen_Last;
}

void Ui_Options_Cats_Destroy() {
    lv_obj_del(container);
}

void redrawoptcats() {
    LcdDma_Mutex_Take(pdMS_TO_TICKS(1000));
    uint8_t cur = 0;
    for (uint8_t i=0;i<OPTION_CATEGORY_COUNT;i++) {
        if (Options_Cat != i) {
            lv_obj_set_style(optioncatlines[i], &optioncatstyle_normal);
            lv_obj_set_style(optioncatlabels[i], &optioncatstyle_normal);
        } else {
            lv_obj_set_style(optioncatlines[i], &optioncatstyle_sel);
            lv_obj_set_style(optioncatlabels[i], &optioncatstyle_sel);
        }
        lv_label_set_static_text(optioncatlabels[i], OptionCatNames[i]);
    }
    LcdDma_Mutex_Give();
}

void Ui_Options_Cats_Key(KeyEvent_t event) {
    if (event.State & KEY_EVENT_PRESS) {
        if (event.Key == KEY_UP) {
            if (Options_Cat) {
                Options_Cat--;
                redrawoptcats();
            }
        } else if (event.Key == KEY_DOWN) {
            if (Options_Cat < OPTION_CATEGORY_COUNT) {
                Options_Cat++;
                redrawoptcats();
            }
        } else if (event.Key == KEY_C) {
            Ui_Screen = UISCREEN_OPTIONS_OPTS;
        } else if (event.Key == KEY_A) {
            Ui_Screen = UISCREEN_MAINMENU;
        } else if (event.Key == KEY_B) {
            Ui_Screen = lastscreen;
        }
    }
}