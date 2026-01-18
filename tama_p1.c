#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bus.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <stm32wbxx_ll_tim.h>
#include <tamalib.h>
#include "tama.h"
#include "views/tama_game.h"
#include "views/tama_menu.h"

typedef enum {
    TamaViewGame,
    TamaViewMenu,
} TamaView;

TamaApp* g_ctx;
FuriMutex* g_state_mutex;
FuriMutex* g_draw_mutex;

static bool tama_p1_navigation_callback(void* callback) {
    furi_assert(callback);

    ViewDispatcher* view_dispatcher = callback;
    view_dispatcher_switch_to_view(view_dispatcher, TamaViewGame);

    return true;
}

static void tama_p1_update_timer_callback(void* callback) {
    furi_assert(callback);

    View* view = callback;
    view_commit_model(view, true);
}

static void tama_p1_load_state() {
    state_t* state;
    uint8_t buf[4];
    bool error = false;
    state = tamalib_get_state();

    if(furi_mutex_acquire(g_state_mutex, FuriWaitForever) != FuriStatusOk) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, TAMA_SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(file, &buf, 4);
        if(buf[0] != (uint8_t)STATE_FILE_MAGIC[0] || buf[1] != (uint8_t)STATE_FILE_MAGIC[1] ||
           buf[2] != (uint8_t)STATE_FILE_MAGIC[2] || buf[3] != (uint8_t)STATE_FILE_MAGIC[3]) {
            FURI_LOG_E(TAG, "FATAL: Wrong state file magic in \"%s\" !\n", TAMA_SAVE_PATH);
            error = true;
        }

        storage_file_read(file, &buf, 1);
        if(buf[0] != STATE_FILE_VERSION) {
            FURI_LOG_E(TAG, "FATAL: Unsupported version");
            error = true;
        }
        if(!error) {
            FURI_LOG_D(TAG, "Reading save.bin");

            storage_file_read(file, &buf, 2);
            *(state->pc) = buf[0] | ((buf[1] & 0x1F) << 8);

            storage_file_read(file, &buf, 2);
            *(state->x) = buf[0] | ((buf[1] & 0xF) << 8);

            storage_file_read(file, &buf, 2);
            *(state->y) = buf[0] | ((buf[1] & 0xF) << 8);

            storage_file_read(file, &buf, 1);
            *(state->a) = buf[0] & 0xF;

            storage_file_read(file, &buf, 1);
            *(state->b) = buf[0] & 0xF;

            storage_file_read(file, &buf, 1);
            *(state->np) = buf[0] & 0x1F;

            storage_file_read(file, &buf, 1);
            *(state->sp) = buf[0];

            storage_file_read(file, &buf, 1);
            *(state->flags) = buf[0] & 0xF;

            storage_file_read(file, &buf, 4);
            *(state->tick_counter) = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

            storage_file_read(file, &buf, 4);
            *(state->clk_timer_timestamp) = buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                                            (buf[3] << 24);

            storage_file_read(file, &buf, 4);
            *(state->prog_timer_timestamp) = buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                                             (buf[3] << 24);

            storage_file_read(file, &buf, 1);
            *(state->prog_timer_enabled) = buf[0] & 0x1;

            storage_file_read(file, &buf, 1);
            *(state->prog_timer_data) = buf[0];

            storage_file_read(file, &buf, 1);
            *(state->prog_timer_rld) = buf[0];

            storage_file_read(file, &buf, 4);
            *(state->call_depth) = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

            FURI_LOG_D(TAG, "Restoring Interupts");
            for(uint32_t i = 0; i < INT_SLOT_NUM; i++) {
                storage_file_read(file, &buf, 1);
                state->interrupts[i].factor_flag_reg = buf[0] & 0xF;

                storage_file_read(file, &buf, 1);
                state->interrupts[i].mask_reg = buf[0] & 0xF;

                storage_file_read(file, &buf, 1);
                state->interrupts[i].triggered = buf[0] & 0x1;
            }

            /* First 640 half bytes correspond to the RAM */
            FURI_LOG_D(TAG, "Restoring RAM");
            for(uint32_t i = 0; i < MEM_RAM_SIZE; i++) {
                storage_file_read(file, &buf, 1);
                SET_RAM_MEMORY(state->memory, i + MEM_RAM_ADDR, buf[0] & 0xF);
            }

            /* I/Os are from 0xF00 to 0xF7F */
            FURI_LOG_D(TAG, "Restoring I/O");
            for(uint32_t i = 0; i < MEM_IO_SIZE; i++) {
                storage_file_read(file, &buf, 1);
                SET_IO_MEMORY(state->memory, i + MEM_IO_ADDR, buf[0] & 0xF);
            }
            FURI_LOG_D(TAG, "Refreshing Hardware");
            tamalib_refresh_hw();
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(g_state_mutex);
}

static void tama_p1_save_state() {
    // Saving state
    FURI_LOG_D(TAG, "Saving Gamestate");

    uint8_t buf[4];
    state_t* state;
    uint32_t offset = 0;
    state = tamalib_get_state();

    if(furi_mutex_acquire(g_state_mutex, FuriWaitForever) != FuriStatusOk) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, TAMA_SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        buf[0] = (uint8_t)STATE_FILE_MAGIC[0];
        buf[1] = (uint8_t)STATE_FILE_MAGIC[1];
        buf[2] = (uint8_t)STATE_FILE_MAGIC[2];
        buf[3] = (uint8_t)STATE_FILE_MAGIC[3];
        offset += storage_file_write(file, &buf, sizeof(buf));

        buf[0] = STATE_FILE_VERSION & 0xFF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->pc) & 0xFF;
        buf[1] = (*(state->pc) >> 8) & 0x1F;
        offset += storage_file_write(file, &buf, 2);

        buf[0] = *(state->x) & 0xFF;
        buf[1] = (*(state->x) >> 8) & 0xF;
        offset += storage_file_write(file, &buf, 2);

        buf[0] = *(state->y) & 0xFF;
        buf[1] = (*(state->y) >> 8) & 0xF;
        offset += storage_file_write(file, &buf, 2);

        buf[0] = *(state->a) & 0xF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->b) & 0xF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->np) & 0x1F;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->sp) & 0xFF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->flags) & 0xF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->tick_counter) & 0xFF;
        buf[1] = (*(state->tick_counter) >> 8) & 0xFF;
        buf[2] = (*(state->tick_counter) >> 16) & 0xFF;
        buf[3] = (*(state->tick_counter) >> 24) & 0xFF;
        offset += storage_file_write(file, &buf, sizeof(buf));

        buf[0] = *(state->clk_timer_timestamp) & 0xFF;
        buf[1] = (*(state->clk_timer_timestamp) >> 8) & 0xFF;
        buf[2] = (*(state->clk_timer_timestamp) >> 16) & 0xFF;
        buf[3] = (*(state->clk_timer_timestamp) >> 24) & 0xFF;
        offset += storage_file_write(file, &buf, sizeof(buf));

        buf[0] = *(state->prog_timer_timestamp) & 0xFF;
        buf[1] = (*(state->prog_timer_timestamp) >> 8) & 0xFF;
        buf[2] = (*(state->prog_timer_timestamp) >> 16) & 0xFF;
        buf[3] = (*(state->prog_timer_timestamp) >> 24) & 0xFF;
        offset += storage_file_write(file, &buf, sizeof(buf));

        buf[0] = *(state->prog_timer_enabled) & 0x1;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->prog_timer_data) & 0xFF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->prog_timer_rld) & 0xFF;
        offset += storage_file_write(file, &buf, 1);

        buf[0] = *(state->call_depth) & 0xFF;
        buf[1] = (*(state->call_depth) >> 8) & 0xFF;
        buf[2] = (*(state->call_depth) >> 16) & 0xFF;
        buf[3] = (*(state->call_depth) >> 24) & 0xFF;
        offset += storage_file_write(file, &buf, sizeof(buf));

        for(uint32_t i = 0; i < INT_SLOT_NUM; i++) {
            buf[0] = state->interrupts[i].factor_flag_reg & 0xF;
            offset += storage_file_write(file, &buf, 1);

            buf[0] = state->interrupts[i].mask_reg & 0xF;
            offset += storage_file_write(file, &buf, 1);

            buf[0] = state->interrupts[i].triggered & 0x1;
            offset += storage_file_write(file, &buf, 1);
        }

        /* First 640 half bytes correspond to the RAM */
        for(uint32_t i = 0; i < MEM_RAM_SIZE; i++) {
            buf[0] = GET_RAM_MEMORY(state->memory, i + MEM_RAM_ADDR) & 0xF;
            offset += storage_file_write(file, &buf, 1);
        }

        /* I/Os are from 0xF00 to 0xF7F */
        for(uint32_t i = 0; i < MEM_IO_SIZE; i++) {
            buf[0] = GET_IO_MEMORY(state->memory, i + MEM_IO_ADDR) & 0xF;
            offset += storage_file_write(file, &buf, 1);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    FURI_LOG_D(TAG, "Finished Writing %lu", offset);
    furi_mutex_release(g_state_mutex);
}

static int32_t tama_p1_worker(void* context) {
    bool running = true;
    FuriMutex* mutex = context;
    while(furi_mutex_acquire(mutex, FuriWaitForever) != FuriStatusOk)
        furi_delay_tick(1);

    cpu_sync_ref_timestamp();
    LL_TIM_EnableCounter(TIM2);

    tama_p1_load_state();

    while(running) {
        if(furi_thread_flags_get()) {
            running = false;
        } else {
            // FURI_LOG_D(TAG, "Stepping");
            // for (int i = 0; i < 100; ++i)
            tamalib_step();
        }
    }

    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }

    LL_TIM_DisableCounter(TIM2);
    furi_hal_bus_disable(FuriHalBusTIM2);
    furi_mutex_release(mutex);
    return 0;
}

static void tama_p1_init(TamaApp* const ctx) {
    g_ctx = ctx;
    memset(ctx, 0, sizeof(TamaApp));
    tama_p1_hal_init(&ctx->hal);

    // Load ROM
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo fi;
    if(storage_common_stat(storage, TAMA_ROM_PATH, &fi) == FSE_OK) {
        File* rom_file = storage_file_alloc(storage);
        if(storage_file_open(rom_file, TAMA_ROM_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            ctx->rom = malloc((size_t)fi.size);
            uint8_t* buf_ptr = ctx->rom;
            size_t read = 0;
            while(read < fi.size) {
                size_t to_read = fi.size - read;
                if(to_read > UINT16_MAX) to_read = UINT16_MAX;
                uint16_t now_read = storage_file_read(rom_file, buf_ptr, (uint16_t)to_read);
                read += now_read;
                buf_ptr += now_read;
            }

            // Reorder endianess of ROM
            for(size_t i = 0; i < fi.size; i += 2) {
                uint8_t b = ctx->rom[i];
                ctx->rom[i] = ctx->rom[i + 1];
                ctx->rom[i + 1] = b & 0xF;
            }
        }

        storage_file_close(rom_file);
        storage_file_free(rom_file);
    }
    furi_record_close(RECORD_STORAGE);

    if(ctx->rom != NULL) {
        // Init TIM2
        furi_hal_bus_enable(FuriHalBusTIM2);
        // 64KHz
        LL_TIM_InitTypeDef tim_init = {
            .Prescaler = 999,
            .CounterMode = LL_TIM_COUNTERMODE_UP,
            .Autoreload = 0xFFFFFFFF,
        };
        LL_TIM_Init(TIM2, &tim_init);
        LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
        LL_TIM_DisableCounter(TIM2);
        LL_TIM_SetCounter(TIM2, 0);

        // Init TamaLIB
        tamalib_register_hal(&ctx->hal);
        tamalib_init((u12_t*)ctx->rom, NULL, 64000);
        tamalib_set_speed(1);

        // TODO: implement fast forwarding
        ctx->fast_forward_done = true;

        // Start stepping thread
        ctx->thread = furi_thread_alloc();
        furi_thread_set_name(ctx->thread, "TamaLIB");
        furi_thread_set_stack_size(ctx->thread, 1024);
        furi_thread_set_callback(ctx->thread, tama_p1_worker);
        furi_thread_set_context(ctx->thread, g_state_mutex);
        furi_thread_start(ctx->thread);
    }
}

static void tama_p1_deinit(TamaApp* const ctx) {
    if(ctx->rom != NULL) {
        tamalib_release();
        furi_thread_free(ctx->thread);
        free(ctx->rom);
    }
}

static void tama_p1_game_callback(TamaGameEventType event_type, void* context) {
    furi_assert(context);

    ViewDispatcher* view_dispatcher = context;

    switch(event_type) {
    case TamaGameEventTypeStop:
        tama_p1_save_state();
        view_dispatcher_stop(view_dispatcher);
        break;

    case TamaGameEventTypeClose:
        view_dispatcher_switch_to_view(view_dispatcher, TamaViewMenu);
        break;
    }
}

static void tama_p1_menu_callback(TamaMenuEventType event_type, void* context) {
    furi_assert(context);

    ViewDispatcher* view_dispatcher = context;

    switch(event_type) {
    case TamaMenuEventTypeSave:
        tama_p1_save_state();
        break;

    case TamaMenuEventTypeLoad:
        tama_p1_load_state();
        break;

    case TamaMenuEventTypeStopNoSave:
        view_dispatcher_stop(view_dispatcher);
        break;

    case TamaMenuEventTypeClose:
        view_dispatcher_switch_to_view(view_dispatcher, TamaViewGame);
        break;
    }
}

int32_t tama_p1_app(void* p) {
    UNUSED(p);

    TamaApp* ctx = malloc(sizeof(TamaApp));
    g_state_mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
    g_draw_mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
    tama_p1_init(ctx);

    Gui* gui = furi_record_open(RECORD_GUI);

    ViewDispatcher* view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(view_dispatcher, view_dispatcher);
    view_dispatcher_set_navigation_event_callback(view_dispatcher, tama_p1_navigation_callback);
    view_dispatcher_attach_to_gui(view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    TamaGame* tama_game = tama_game_alloc();
    tama_game_set_callback(tama_game, tama_p1_game_callback, view_dispatcher);
    view_dispatcher_add_view(view_dispatcher, TamaViewGame, tama_game_get_view(tama_game));

    TamaMenu* tama_menu = tama_menu_alloc();
    tama_menu_set_callback(tama_menu, tama_p1_menu_callback, view_dispatcher);
    view_dispatcher_add_view(view_dispatcher, TamaViewMenu, tama_menu_get_view(tama_menu));

    ctx->timer = furi_timer_alloc(
        tama_p1_update_timer_callback, FuriTimerTypePeriodic, tama_game_get_view(tama_game));

    view_dispatcher_switch_to_view(view_dispatcher, TamaViewGame);
    view_dispatcher_run(view_dispatcher);

    if(ctx->rom != NULL) {
        furi_thread_flags_set(furi_thread_get_id(ctx->thread), 1);
        furi_thread_join(ctx->thread);
    }

    furi_timer_free(ctx->timer);
    ctx->timer = NULL;

    view_dispatcher_remove_view(view_dispatcher, TamaViewGame);
    view_dispatcher_remove_view(view_dispatcher, TamaViewMenu);
    tama_game_free(tama_game);
    tama_menu_free(tama_menu);
    view_dispatcher_free(view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_mutex_free(g_state_mutex);
    furi_mutex_free(g_draw_mutex);
    tama_p1_deinit(ctx);
    free(ctx);

    return 0;
}
