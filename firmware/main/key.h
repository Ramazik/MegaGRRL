#ifndef AGR_KEY_H
#define AGR_KEY_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mallocs.h"
#include "esp_system.h"

typedef struct KeyState {
    uint8_t RawState;
    uint32_t RawStateTs;
    uint8_t DebouncedState;
    uint32_t TsDown; //debounced
    uint32_t TsUp; //debounced
    uint32_t TsLastRepeat;
    bool AlreadyHeld;
} KeyState_t;

typedef struct KeyEvent {
    uint8_t Key;
    uint8_t State;
} KeyEvent_t;

#define KEY_EVENT_DOWN      0x1
#define KEY_EVENT_UP        0x2
#define KEY_EVENT_PRESS     0x4
#define KEY_EVENT_REPEAT    0x8 //used like a flag
#define KEY_EVENT_HOLD      0x10

#define KEY_A       0
#define KEY_B       1
#define KEY_C       2
#define KEY_DOWN    3
#define KEY_RIGHT   4
#define KEY_LEFT    5
#define KEY_UP      6
#define KEY_COUNT   7

#define KEY_DEBOUNCE_TIME 30
#define KEY_REPEAT_INTERVAL 100
#define KEY_REPEAT_DELAY 750
#define KEY_HOLD_DELAY 750

bool KeyMgr_Setup();
void KeyMgr_Main();

volatile QueueHandle_t KeyMgr_TargetQueue;

#endif