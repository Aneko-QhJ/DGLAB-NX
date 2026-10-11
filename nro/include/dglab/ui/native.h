#pragma once
#include <switch.h>
#include <dglab/ui/about.h>
#include <dglab/ui/menu.h>
#include <dglab/nro/motion_settings.h>
#include <dglab/ui/ble.h>
#include <dglab/ui/motion.h>
#include <dglab/ui/touch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DglabMenuResult_Exit = 0,
    DglabMenuResult_Socket, DglabMenuResult_Ble,
    DglabMenuResult_Motion, DglabMenuResult_Touch,
    DglabMenuResult_Advanced, DglabMenuResult_About,
} DglabMenuResult;

// Borrowed state is valid until the next update/leave. Views copy dynamic text.
typedef struct {
    DglabMenuResult page;
    union {
        DglabScreenState socket;
        DglabBlePageState ble;
        DglabMotionScreenState motion;
        DglabTouchScreenState touch;
    } value;
    DglabLogPage log;
    u32 log_generation;
} DglabGameplayState;

typedef struct {
    Service* service;
    PadState* pad;
    unsigned* menu_selected;
    void (*about_state)(DglabAboutState* state);
    void (*cycle_language)(void);
    void (*cycle_theme)(void);
    bool (*is_dark)(void);
    bool (*sysmodule_ok)(Service* service);
    bool (*ble_active)(Service* service);
    void (*load_config)(DglabMotionFeedConfig* config);
    bool (*save_config)(const DglabMotionFeedConfig* config);
    void (*poll)(void);
    void (*log)(const char* line);
    void (*page_enter)(Service* service, PadState* pad, DglabMenuResult page);
    bool (*page_update)(u64 down, u64 held, u64 now_ns);
    const DglabGameplayState* (*page_state)(void);
    void (*page_leave)(void);
    void (*exit_cleanup)(Service* service);
} DglabNativeHost;

bool dglabNativeInit(const DglabNativeHost* host);
bool dglabNativeResume(void);
void dglabNativeSuspend(void);
void dglabNativeShutdown(void);
// Runs all ordinary pages until exit; failures return to the startup diagnostic.
DglabMenuResult dglabNativeRun(void);
const char* dglabNativeError(void);

#ifdef __cplusplus
}
#endif
