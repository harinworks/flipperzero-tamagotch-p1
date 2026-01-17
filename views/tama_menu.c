#include <furi_hal.h>
#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include "../tama.h"
#include "tama_menu.h"

typedef struct TamaMenu {
    VariableItemList* list;
    TamaMenuCallback callback;
    void* context;
} TamaMenu;

typedef enum {
    TamaMenuItemSave,
    TamaMenuItemLoad,
    TamaMenuItemSpeed,
    TamaMenuItemMute,
} TamaMenuItem;

static const char* cpu_speed_names[] = {"Off", "2x", "4x"};
static const char* buzzer_mute_names[] = {"Off", "On"};

static void tama_cpu_speed_change_callback(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, cpu_speed_names[index]);

    if(furi_mutex_acquire(g_state_mutex, FuriWaitForever) != FuriStatusOk) return;

    g_ctx->cpu_speed = index;
    tamalib_set_speed(1 << index);
    furi_mutex_release(g_state_mutex);
}

static void tama_buzzer_mute_change_callback(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, buzzer_mute_names[index]);

    if(furi_mutex_acquire(g_state_mutex, FuriWaitForever) != FuriStatusOk) return;

    g_ctx->buzzer_mute = index == 1;

    if(g_ctx->buzzer_mute && furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }

    furi_mutex_release(g_state_mutex);
}

static void tama_menu_callback(void* context, uint32_t index) {
    furi_assert(context);

    TamaMenu* tama_menu = context;

    switch(index) {
    case TamaMenuItemSave:
        if(tama_menu->callback) tama_menu->callback(TamaMenuEventTypeSave, tama_menu->context);
        break;

    case TamaMenuItemLoad:
        if(tama_menu->callback) tama_menu->callback(TamaMenuEventTypeLoad, tama_menu->context);
        break;
    }

    if(tama_menu->callback) tama_menu->callback(TamaMenuEventTypeClose, tama_menu->context);
}

TamaMenu* tama_menu_alloc() {
    TamaMenu* tama_menu = malloc(sizeof(TamaMenu));
    tama_menu->list = variable_item_list_alloc();

    VariableItem* item;

    variable_item_list_add(tama_menu->list, "Save State", 0, NULL, NULL);
    variable_item_list_add(tama_menu->list, "Load State", 0, NULL, NULL);

    item = variable_item_list_add(
        tama_menu->list, "CPU Speed", 3, tama_cpu_speed_change_callback, NULL);
    variable_item_set_current_value_index(item, g_ctx->cpu_speed);
    variable_item_set_current_value_text(item, cpu_speed_names[g_ctx->cpu_speed]);

    item = variable_item_list_add(
        tama_menu->list, "Buzzer Mute", 2, tama_buzzer_mute_change_callback, NULL);
    variable_item_set_current_value_index(item, g_ctx->buzzer_mute ? 1 : 0);
    variable_item_set_current_value_text(item, buzzer_mute_names[g_ctx->buzzer_mute ? 1 : 0]);

    variable_item_list_set_enter_callback(tama_menu->list, tama_menu_callback, tama_menu);

    return tama_menu;
}

void tama_menu_free(TamaMenu* tama_menu) {
    furi_assert(tama_menu);
    variable_item_list_free(tama_menu->list);
    free(tama_menu);
}

View* tama_menu_get_view(TamaMenu* tama_menu) {
    furi_assert(tama_menu);
    return variable_item_list_get_view(tama_menu->list);
}

void tama_menu_set_callback(TamaMenu* tama_menu, TamaMenuCallback callback, void* context) {
    furi_assert(tama_menu);
    tama_menu->callback = callback;
    tama_menu->context = context;
}
