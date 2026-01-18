#pragma once

#include <input/input.h>
#include <tamalib.h>

#define TAG                      "TamaP1"
#define TAMA_BASE_PATH           EXT_PATH("tama_p1/")
#define TAMA_ROM_PATH            (TAMA_BASE_PATH "rom.bin")
#define TAMA_SCREEN_SCALE_FACTOR 2
#define TAMA_LCD_ICON_SIZE       14
#define TAMA_LCD_ICON_MARGIN     1

#define STATE_FILE_MAGIC   "TLST"
#define STATE_FILE_VERSION 2

typedef struct {
    FuriThread* thread;
    FuriTimer* timer;
    hal_t hal;
    uint8_t* rom;
    // 32x16 screen, perfectly represented through uint32_t
    uint32_t framebuffer[16];
    uint8_t icons;
    bool halted;
    bool fast_forward_done;
    bool buzzer_on;
    float frequency;
    uint8_t cpu_speed;
    bool buzzer_mute;
} TamaApp;

typedef enum {
    EventTypeInput,
    EventTypeTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} TamaEvent;

extern TamaApp* g_ctx;
extern FuriMutex* g_state_mutex;
extern FuriMutex* g_draw_mutex;

void tama_p1_hal_init(hal_t* hal);
