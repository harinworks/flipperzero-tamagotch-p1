#pragma once

#include <gui/view.h>

typedef enum {
    TamaMenuEventTypeSave,
    TamaMenuEventTypeLoad,
    TamaMenuEventTypeReset,
    TamaMenuEventTypeBrowse,
    TamaMenuEventTypeStopNoSave,
    TamaMenuEventTypeClose,
} TamaMenuEventType;

typedef struct TamaMenu TamaMenu;
typedef void (*TamaMenuCallback)(TamaMenuEventType event_type, void* context);

TamaMenu* tama_menu_alloc();
void tama_menu_free(TamaMenu* tama_menu);
View* tama_menu_get_view(TamaMenu* tama_menu);
void tama_menu_set_callback(TamaMenu* tama_menu, TamaMenuCallback callback, void* context);
