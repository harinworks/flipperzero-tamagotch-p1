#include <gui/view.h>
#include "../tama.h"
#include "tama_game.h"
#include "compiled/assets_icons.h"

typedef struct TamaGame {
    View* view;
    TamaGameCallback callback;
    void* context;
} TamaGame;

static const Icon* icons_list[] = {
    &I_icon_0,
    &I_icon_1,
    &I_icon_2,
    &I_icon_3,
    &I_icon_4,
    &I_icon_5,
    &I_icon_6,
    &I_icon_7,
};

static void tama_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);

    if(furi_mutex_acquire(g_draw_mutex, 25) != FuriStatusOk) return;

    if(g_ctx->rom == NULL) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 30, 30, "No ROM");
    } else if(g_ctx->halted) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 30, 30, "Halted");
    } else {
        // FURI_LOG_D(TAG, "Drawing frame");
        // Calculate positioning
        uint16_t canv_width = canvas_width(canvas);
        // uint16_t canv_height = canvas_height(canvas);
        uint16_t lcd_matrix_scaled_width = 32 * TAMA_SCREEN_SCALE_FACTOR;
        // uint16_t lcd_matrix_scaled_height = 16 * TAMA_SCREEN_SCALE_FACTOR;
        uint16_t lcd_matrix_top = 0;
        uint16_t lcd_matrix_left = (canv_width - lcd_matrix_scaled_width) / 2;

        /*
        uint16_t lcd_icon_upper_top = lcd_matrix_top - TAMA_LCD_ICON_SIZE - TAMA_LCD_ICON_MARGIN;
        uint16_t lcd_icon_upper_left = lcd_matrix_left;
        uint16_t lcd_icon_lower_top =
            lcd_matrix_top + lcd_matrix_scaled_height + TAMA_LCD_ICON_MARGIN;
        uint16_t lcd_icon_lower_left = lcd_matrix_left;
        uint16_t lcd_icon_spacing_horiz =
            (lcd_matrix_scaled_width - (4 * TAMA_LCD_ICON_SIZE)) / 3 + TAMA_LCD_ICON_SIZE;
        */

        uint16_t y = lcd_matrix_top;
        for(uint8_t row = 0; row < 16; ++row) {
            uint16_t x = lcd_matrix_left;
            uint32_t row_pixels = g_ctx->framebuffer[row];
            for(uint8_t col = 0; col < 32; ++col) {
                if(row_pixels & 1) {
                    canvas_draw_box(
                        canvas, x, y, TAMA_SCREEN_SCALE_FACTOR, TAMA_SCREEN_SCALE_FACTOR);
                }
                x += TAMA_SCREEN_SCALE_FACTOR;
                row_pixels >>= 1;
            }
            y += TAMA_SCREEN_SCALE_FACTOR;
        }

        // Draw Icons on bottom
        uint8_t lcd_icons = g_ctx->icons;
        uint16_t x_ic = 0;
        y = 64 - TAMA_LCD_ICON_SIZE;
        for(uint8_t i = 0; i < 7; ++i) {
            if(lcd_icons & 1) {
                canvas_draw_icon(canvas, x_ic, y, icons_list[i]);
            }
            x_ic += TAMA_LCD_ICON_SIZE + 4;
            lcd_icons >>= 1;
        }

        if(lcd_icons & 7) {
            canvas_draw_icon(canvas, 128 - TAMA_LCD_ICON_SIZE, 0, icons_list[7]);
        }
    }

    furi_mutex_release(g_draw_mutex);
}

static bool tama_input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);

    TamaGame* tama_game = context;

    if(furi_mutex_acquire(g_state_mutex, FuriWaitForever) != FuriStatusOk) return false;

    FURI_LOG_D(
        TAG,
        "EventTypeInput: %ld %d %d",
        input_event->sequence,
        input_event->key,
        input_event->type);
    InputType input_type = input_event->type;

    if(input_type == InputTypePress || input_type == InputTypeRelease) {
        btn_state_t tama_btn_state = 0;
        if(input_type == InputTypePress)
            tama_btn_state = BTN_STATE_PRESSED;
        else if(input_type == InputTypeRelease)
            tama_btn_state = BTN_STATE_RELEASED;

        if(input_event->key == InputKeyLeft)
            tamalib_set_button(BTN_LEFT, tama_btn_state);
        else if(input_event->key == InputKeyOk)
            tamalib_set_button(BTN_MIDDLE, tama_btn_state);
        else if(input_event->key == InputKeyRight)
            tamalib_set_button(BTN_RIGHT, tama_btn_state);
    } else if(input_event->key == InputKeyBack) {
        if(input_event->type == InputTypeShort) {
            if(tama_game->callback)
                tama_game->callback(TamaGameEventTypeClose, tama_game->context);
        } else if(input_event->type == InputTypeLong) {
            if(tama_game->callback) tama_game->callback(TamaGameEventTypeStop, tama_game->context);
        }
    }

    furi_mutex_release(g_state_mutex);

    return true;
}

static void tama_game_enter_callback(void* context) {
    UNUSED(context);

    if(g_ctx->timer) furi_timer_start(g_ctx->timer, furi_kernel_get_tick_frequency() / 30);
}

static void tama_game_exit_callback(void* context) {
    UNUSED(context);

    if(g_ctx->timer) furi_timer_stop(g_ctx->timer);
}

TamaGame* tama_game_alloc() {
    TamaGame* tama_game = malloc(sizeof(TamaGame));
    tama_game->view = view_alloc();

    view_set_context(tama_game->view, tama_game);
    view_set_draw_callback(tama_game->view, tama_draw_callback);
    view_set_input_callback(tama_game->view, tama_input_callback);
    view_set_enter_callback(tama_game->view, tama_game_enter_callback);
    view_set_exit_callback(tama_game->view, tama_game_exit_callback);

    return tama_game;
}

void tama_game_free(TamaGame* tama_game) {
    furi_assert(tama_game);
    view_free(tama_game->view);
    free(tama_game);
}

View* tama_game_get_view(TamaGame* tama_game) {
    furi_assert(tama_game);
    return tama_game->view;
}

void tama_game_set_callback(TamaGame* tama_game, TamaGameCallback callback, void* context) {
    furi_assert(tama_game);
    tama_game->callback = callback;
    tama_game->context = context;
}
