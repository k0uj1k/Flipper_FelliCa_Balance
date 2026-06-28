#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/felica/felica_poller.h>
#include <toolbox/simple_array.h>

#define TAG "FelicaBalance"
#define EXIT_FLAG (1UL << 0)

typedef struct {
    FuriMutex* mutex;
    uint32_t balance;
    bool has_balance;
    bool no_balance_info;
    bool is_reading;
    bool is_error;
    char card_name[32];
    FuriThread* worker_thread;
    volatile bool worker_running;
    Nfc* nfc;
    NfcPoller* poller;
} FelicaBalanceApp;

static void format_balance(uint32_t balance, char* out, size_t out_size) {
    if(balance < 1000) {
        snprintf(out, out_size, "%lu", balance);
    } else if(balance < 1000000) {
        snprintf(out, out_size, "%lu,%03lu", balance / 1000, balance % 1000);
    } else {
        snprintf(out, out_size, "%lu,%03lu,%03lu", balance / 1000000, (balance % 1000000) / 1000, balance % 1000);
    }
}

static void draw_callback(Canvas* canvas, void* ctx) {
    FelicaBalanceApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    
    // Draw title border/header
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "FeliCa Balance Reader");
    canvas_draw_line(canvas, 0, 15, 128, 15);

    if (app->is_reading) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Reading card...");
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "Hold card still");
    } else if (app->is_error) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Read error!");
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "(Connection failed?)");
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignCenter, "Press OK to try again");
    } else if (app->no_balance_info) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, app->card_name);
        canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignCenter, "No balance info");
        canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, "Press OK to scan again");
    } else if (app->has_balance) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, app->card_name);

        canvas_set_font(canvas, FontBigNumbers);
        char balance_str[32];
        format_balance(app->balance, balance_str, sizeof(balance_str));
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, balance_str);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, "Press OK to scan again");
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Hold FeliCa card to back");
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "(Suica, PASMO, Edy, etc.)");
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static NfcCommand felica_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolFelica);
    furi_assert(event.event_data);

    FelicaBalanceApp* app = context;
    FelicaPoller* felica_poller = event.instance;
    FelicaPollerEvent* felica_event = event.event_data;

    // Handle authentication context request by immediately skipping auth
    if (felica_event->type == FelicaPollerEventTypeRequestAuthContext) {
        FURI_LOG_I(TAG, "FeliCa card detected. Intercepting polling workflow to prevent reset...");
        
        felica_event->data->auth_context->skip_auth = true;

        // Bypassing the standard reading loop (service traversal) to prevent crash
        FelicaData* felica_data = (FelicaData*)nfc_poller_get_data(app->poller);
        if (felica_data != NULL) {
            felica_data->workflow_type = FelicaUnknown;
        }

        // Direct reading of FeliCa blocks without traversing services
        uint8_t block_numbers[] = {0};
        FelicaPollerReadCommandResponse* response = NULL;
        
        // 1. Try Suica/PASMO Transit Card (Service 0x090f)
        FURI_LOG_I(TAG, "Trying service 0x090f (Transit)...");
        FelicaError err = felica_poller_read_blocks(felica_poller, 1, block_numbers, 0x090f, &response);
        if (err == FelicaErrorNone && response != NULL && response->SF1 == 0 && response->SF2 == 0 && response->block_count > 0) {
            uint32_t bal = (uint32_t)response->data[10] | ((uint32_t)response->data[11] << 8);
            FURI_LOG_I(TAG, "Suica/PASMO balance found: %lu YEN", bal);
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->balance = bal;
            app->has_balance = true;
            app->is_reading = false;
            app->is_error = false;
            app->no_balance_info = false;
            strncpy(app->card_name, "Transit Card (Suica/PASMO)", sizeof(app->card_name));
            furi_mutex_release(app->mutex);
        } else {
            // 2. Try Edy Card (Service 0x170f)
            FURI_LOG_I(TAG, "Trying service 0x170f (Edy)...");
            err = felica_poller_read_blocks(felica_poller, 1, block_numbers, 0x170f, &response);
            if (err == FelicaErrorNone && response != NULL && response->SF1 == 0 && response->SF2 == 0 && response->block_count > 0) {
                uint32_t bal = (uint32_t)response->data[0] |
                               ((uint32_t)response->data[1] << 8) |
                               ((uint32_t)response->data[2] << 16) |
                               ((uint32_t)response->data[3] << 24);
                FURI_LOG_I(TAG, "Edy balance found: %lu YEN", bal);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->balance = bal;
                app->has_balance = true;
                app->is_reading = false;
                app->is_error = false;
                app->no_balance_info = false;
                strncpy(app->card_name, "Edy Card", sizeof(app->card_name));
                furi_mutex_release(app->mutex);
            } else {
                // 3. Try Nanaco Card (Service 0x5597)
                FURI_LOG_I(TAG, "Trying service 0x5597 (nanaco)...");
                err = felica_poller_read_blocks(felica_poller, 1, block_numbers, 0x5597, &response);
                if (err == FelicaErrorNone && response != NULL && response->SF1 == 0 && response->SF2 == 0 && response->block_count > 0) {
                    uint32_t bal = (uint32_t)response->data[0] |
                                   ((uint32_t)response->data[1] << 8) |
                                   ((uint32_t)response->data[2] << 16) |
                                   ((uint32_t)response->data[3] << 24);
                    FURI_LOG_I(TAG, "nanaco balance found: %lu YEN", bal);
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->balance = bal;
                    app->has_balance = true;
                    app->is_reading = false;
                    app->is_error = false;
                    app->no_balance_info = false;
                    strncpy(app->card_name, "nanaco Card", sizeof(app->card_name));
                    furi_mutex_release(app->mutex);
                } else {
                    // 4. Try Waon Card (Service 0x6817)
                    FURI_LOG_I(TAG, "Trying service 0x6817 (WAON)...");
                    err = felica_poller_read_blocks(felica_poller, 1, block_numbers, 0x6817, &response);
                    if (err == FelicaErrorNone && response != NULL && response->SF1 == 0 && response->SF2 == 0 && response->block_count > 0) {
                        uint32_t bal = (uint32_t)response->data[0] |
                                       ((uint32_t)response->data[1] << 8) |
                                       ((uint32_t)response->data[2] << 16);
                        FURI_LOG_I(TAG, "WAON balance found: %lu YEN", bal);
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        app->balance = bal;
                        app->has_balance = true;
                        app->is_reading = false;
                        app->is_error = false;
                        app->no_balance_info = false;
                        strncpy(app->card_name, "WAON Card", sizeof(app->card_name));
                        furi_mutex_release(app->mutex);
                    } else {
                        FURI_LOG_E(TAG, "FeliCa card does not contain recognized balance services");
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        app->no_balance_info = true;
                        app->is_reading = false;
                        app->is_error = false;
                        app->has_balance = false;
                        strncpy(app->card_name, "FeliCa Card", sizeof(app->card_name));
                        furi_mutex_release(app->mutex);
                    }
                }
            }
        }
        
        return NfcCommandStop;
    } else if (felica_event->type == FelicaPollerEventTypeReady ||
               felica_event->type == FelicaPollerEventTypeIncomplete) {
        FURI_LOG_I(TAG, "Polling loop complete (Ready/Incomplete)");
        return NfcCommandStop;
    } else if (felica_event->type == FelicaPollerEventTypeError) {
        FURI_LOG_E(TAG, "Poller error: %d", felica_event->data->error);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->is_error = true;
        app->is_reading = false;
        furi_mutex_release(app->mutex);
        return NfcCommandStop;
    }

    return NfcCommandContinue;
}

static int32_t nfc_worker_thread(void* context) {
    FelicaBalanceApp* app = context;
    FURI_LOG_I(TAG, "NFC worker thread started");
    
    while (app->worker_running) {
        bool should_poll = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if (!app->has_balance && !app->no_balance_info && !app->is_error && !app->is_reading) {
            app->is_reading = true;
            should_poll = true;
        }
        furi_mutex_release(app->mutex);
        
        if (should_poll) {
            FURI_LOG_I(TAG, "Starting NFC polling...");
            app->nfc = nfc_alloc();
            app->poller = nfc_poller_alloc(app->nfc, NfcProtocolFelica);
            
            nfc_poller_start(app->poller, felica_poller_callback, app);
            
            while (app->worker_running) {
                bool done = false;
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if (!app->is_reading) {
                    done = true;
                }
                furi_mutex_release(app->mutex);
                
                if (done) break;
                furi_delay_ms(100);
            }
            
            nfc_poller_stop(app->poller);
            nfc_poller_free(app->poller);
            nfc_free(app->nfc);
            
            app->poller = NULL;
            app->nfc = NULL;
            FURI_LOG_I(TAG, "NFC polling stopped");
        }
        
        furi_delay_ms(100);
    }
    
    FURI_LOG_I(TAG, "NFC worker thread stopping");
    return 0;
}

int32_t felica_balance_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "App started");
    
    // Allocate app context
    FelicaBalanceApp* app = malloc(sizeof(FelicaBalanceApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->balance = 0;
    app->has_balance = false;
    app->no_balance_info = false;
    app->is_reading = false;
    app->is_error = false;
    app->worker_running = true;
    app->poller = NULL;
    app->nfc = NULL;
    memset(app->card_name, 0, sizeof(app->card_name));

    // Allocate message queue for input events
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Configure Viewport
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Register Viewport in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Create and start worker thread
    app->worker_thread = furi_thread_alloc_ex("FelicaNfcWorker", 4096, nfc_worker_thread, app);
    furi_thread_start(app->worker_thread);

    // Event loop
    InputEvent event;
    while (true) {
        FuriStatus status = furi_message_queue_get(event_queue, &event, 100);
        
        if (status == FuriStatusOk) {
            // Press Back to exit
            if (event.type == InputTypeShort && event.key == InputKeyBack) {
                FURI_LOG_I(TAG, "Back key pressed, exiting");
                break;
            }
            // Press OK to retry/scan again
            if (event.type == InputTypeShort && event.key == InputKeyOk) {
                FURI_LOG_I(TAG, "Ok key pressed, resetting scanner state");
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->has_balance = false;
                app->no_balance_info = false;
                app->is_error = false;
                app->is_reading = false;
                furi_mutex_release(app->mutex);
            }
        }
        
        // Redraw screen
        view_port_update(view_port);
    }

    FURI_LOG_I(TAG, "App stopping...");

    // Clean up worker thread
    app->worker_running = false;
    
    // Wake up worker thread if blocked in wait
    FURI_LOG_I(TAG, "Waking up NFC worker thread");
    furi_thread_flags_set(furi_thread_get_id(app->worker_thread), EXIT_FLAG);

    FURI_LOG_I(TAG, "Joining NFC worker thread...");
    furi_thread_join(app->worker_thread);
    FURI_LOG_I(TAG, "NFC worker thread joined. Freeing thread...");
    furi_thread_free(app->worker_thread);

    // Clean up GUI
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);

    // Free message queue
    furi_message_queue_free(event_queue);

    // Free mutex and app context
    furi_mutex_free(app->mutex);
    free(app);

    FURI_LOG_I(TAG, "App exited successfully");
    return 0;
}
