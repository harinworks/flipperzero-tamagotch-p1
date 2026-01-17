#pragma once

#include <gui/view.h>

typedef enum {
    TamaGameEventTypeStop,
    TamaGameEventTypeClose,
} TamaGameEventType;

typedef struct TamaGame TamaGame;
typedef void (*TamaGameCallback)(TamaGameEventType event_type, void* context);

TamaGame* tama_game_alloc();
void tama_game_free(TamaGame* tama_game);
View* tama_game_get_view(TamaGame* tama_game);
void tama_game_set_callback(TamaGame* tama_game, TamaGameCallback callback, void* context);
