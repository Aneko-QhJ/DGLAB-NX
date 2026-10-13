// Execute the actual app controller with mock IPC/HID and the real waveform cores.
#define DGLAB_UI_BOREALIS 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <dglab/ui/native.h>
#include <dglab/ui/strings.h>
#include <dglab/ui/input_state.h>
#include <dglab/ui/page.h>
#include <dglab/ipc_poc.h>
#include <dglab/nro/joycon.h>
#include <dglab/nro/touch_feed.h>
#include <dglab/platform/touchscreen.h>

#define LOG_POLL_INTERVAL_FRAMES 3
#define MOTION_DRAIN_MAX 17u
#define MOTION_CHANNEL_A 1u
#define MOTION_CHANNEL_B 2u

typedef struct {
    char lines[DGLAB_SCREEN_LOG_LINES][DGLAB_SCREEN_LOG_LINE_LEN];
    const char* pointers[DGLAB_SCREEN_LOG_LINES];
    int filled;
    u32 generation;
} DglabLogRing;
static DglabLogRing g_log_net, g_log_ble;
static u32 g_test_strength_a, g_test_strength_b;
static char g_last_command[40];
static u32 g_last_command_tone;
static u64 g_strength_hold_started_ns, g_strength_last_repeat_ns;
static u64 g_log_hold_started_ns, g_log_last_repeat_ns;
static bool g_ble_limits_known;
static u32 g_ble_limit_a, g_ble_limit_b;
static Service service = {42};
static PadState pad;
static u64 now_ns = 1000000000;
static unsigned stops, clears, uploads, sensors_started, sensors_stopped;
static unsigned touch_started, touch_polled, motion_polled, adjustments, scrolls, rescans;
static unsigned net_starts, net_stops, preparation_starts, ble_starts, ble_stops;
static DglabBlePatchResult patch_result;
static DglabBleEditResult edit_result;
static unsigned edits;
static DglabBlePatchResult appBlePatchCheck(void) { return patch_result; }
static DglabBleEditResult bleEditAddress(u8 out[6]) {
    ++edits;
    if (edit_result == DglabBleEdit_Saved) memset(out, 0xAA, 6);
    pad.buttons = HidNpadButton_A | HidNpadButton_B;
    return edit_result;
}
static bool address_available = true, asleep_suppressed, status_failure;
static DglabNetStatus net_status;
static DglabBleStatus ble_status;
static DglabPocStatus preparation;
static DglabTouchFrame touch_readings;
static Result preparation_error;
static void appPageTrace(const char* event, DglabMenuResult page, u64 elapsed)
{ (void)event; (void)page; (void)elapsed; }
static DglabBleStartRequest last_ble_start;
static DglabNetWaveformSlot last_slots[DGLAB_NET_WAVEFORM_MAX_SLOTS];
static size_t last_slot_count;

u64 armGetSystemTick(void) { return now_ns; }
u32 hidGetNpadDeviceType(unsigned id) { (void)id; return HidDeviceTypeBits_JoyLeft | HidDeviceTypeBits_JoyRight; }
Result testDispatch(Service* s, unsigned command, const void* input, size_t input_size, void* output, size_t output_size)
{
    assert(s == &service); // All child pages must share the app's one session.
    if (output) memset(output, 0, output_size);
    switch (command) {
        case DGLAB_IPC_CMD_NET_STATUS:
            if (status_failure) return 1;
            assert(output_size == sizeof(net_status)); memcpy(output, &net_status, output_size); break;
        case DGLAB_IPC_CMD_NET_QR:
            assert(output_size == sizeof(DglabNetQrChunk));
            snprintf(((DglabNetQrChunk*)output)->text, DGLAB_NET_QR_MAX, "ws://127.0.0.1:9999/test"); break;
        case DGLAB_IPC_CMD_BLE_STATUS:
            assert(output_size == sizeof(ble_status)); memcpy(output, &ble_status, output_size); break;
        case DGLAB_IPC_POC_CMD_STATUS:
            if (preparation_error) return preparation_error;
            assert(output_size == sizeof(preparation)); memcpy(output, &preparation, output_size); break;
        case DGLAB_IPC_POC_CMD_START: ++preparation_starts; break;
        case DGLAB_IPC_POC_CMD_ACTION: break;
        case DGLAB_IPC_POC_CMD_STOP: ++stops; break;
        case DGLAB_IPC_CMD_BLE_STOP: ++ble_stops; break;
        case DGLAB_IPC_CMD_BLE_START:
            assert(input_size == sizeof(last_ble_start));
            memcpy(&last_ble_start, input, input_size); ++ble_starts; break;
        default: assert(!"unexpected IPC command");
    }
    return 0;
}
const char* dglabString(DglabString key) { (void)key; return "test"; }
const char* dglabNetStateText(u32 state) { (void)state; return "state"; }
static void motionSettingsLoad(DglabMotionFeedConfig* config) { dglabMotionSettingsDefault(config); }
static bool appSysmoduleOk(Service* s) { assert(s == &service); return !status_failure; }
static bool appBleSessionActive(Service* s) { assert(s == &service); return ble_status.state == DglabBleState_Connected; }
static void appAutoSleepFollow(bool running) { asleep_suppressed = running; }
bool dglabAutoSleepActive(void) { return asleep_suppressed; }
static void logPoll(Service* s) { assert(s == &service); }
static void bleLogPoll(Service* s) { assert(s == &service); }
static void buildLogPointers(DglabLogRing* ring) { for (int i = 0; i < ring->filled; ++i) ring->pointers[i] = ring->lines[i]; }
static void logPushLine(DglabLogRing* ring, const char* value) {
    if (ring->filled < DGLAB_SCREEN_LOG_LINES) snprintf(ring->lines[ring->filled++], DGLAB_SCREEN_LOG_LINE_LEN, "%.*s", DGLAB_SCREEN_LOG_LINE_LEN - 1, value);
    ++ring->generation;
}
static void noteCommand(const char* name, Result rc, const char* note) { (void)name; (void)rc; (void)note; }
static Result sendTestCommand(Service* s, u32 command, u32 channel, u32 value) {
    assert(s == &service); (void)channel; (void)value;
    if (command == DglabNetCommand_Clear) ++clears;
    return 0;
}
static void toggleServer(Service* s, bool running) { assert(s == &service); if (running) ++net_stops; else ++net_starts; }
static void testChannelButtons(Service* s, u64 down) { assert(s == &service); if (down & HidNpadButton_X) ++clears; }
static void adjustStrengthFromDirections(Service* s, u64 down, u32 a, u32 b) {
    assert(s == &service); (void)a; (void)b;
    if (down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)) ++adjustments;
}
static void repeatStrengthFromDirections(Service* s, u64 held, u64 now, u32 a, u32 b) {
    (void)s; (void)now; (void)a; (void)b;
    if (held) g_strength_hold_started_ns = now;
}
static void clampStrengthsToCeilings(Service* s, u32 a, u32 b, bool send) {
    assert(s == &service); (void)send;
    if (g_test_strength_a > a) g_test_strength_a = a;
    if (g_test_strength_b > b) g_test_strength_b = b;
}
static bool slotsHaveStrength(const DglabNetWaveformSlot* slots, size_t count) {
    for (size_t i = 0; i < count; ++i) if (slots[i].strength) return true;
    return false;
}
static Result uploadSlots(Service* s, u32 channel, const DglabNetWaveformSlot* slots, size_t count) {
    assert(s == &service); assert(channel == 1 || channel == 2); assert(count <= DGLAB_NET_WAVEFORM_MAX_SLOTS);
    memcpy(last_slots, slots, count * sizeof(*slots)); last_slot_count = count; ++uploads; return 0;
}
static DglabString touchChannelCommand(unsigned channel) { return channel ? DglabString_CmdWaveformB : DglabString_CmdWaveformA; }
static u32 touchChannelNumber(unsigned channel) { return channel + 1; }
static void touchLogLine(const char* tag) { (void)tag; }
static void bleLogFailure(const char* name, Result rc) { (void)name; (void)rc; }
static void bleLogCallTiming(const char* name, u64 start) { (void)name; (void)start; }
static bool loadBleAddress(u8 address[6]) { memset(address, 1, 6); return address_available; }
static bool bleStateIsActive(u32 state) { return state == DglabBleState_Connecting || state == DglabBleState_Connected; }
static void blePushLimits(Service* s, u32 a, u32 b) { assert(s == &service); g_ble_limit_a = a; g_ble_limit_b = b; g_ble_limits_known = true; }
static int logMaxOffset(int count) { int height = count * DGLAB_SCREEN_LOG_PITCH - (DGLAB_PAGE_CONTENT_BOTTOM - DGLAB_PAGE_CONTENT_TOP); return height > 0 ? height : 0; }
static int logScrollFromDirections(int offset, int max, u64 down, u64 held, u64 now) {
    (void)held; (void)now;
    if (down & (HidNpadButton_Up | HidNpadButton_Down)) ++scrolls;
    if (offset > max) offset = max;
    return offset;
}
bool dglabJoyconStart(void) { ++sensors_started; return true; }
void dglabJoyconStop(void) { ++sensors_stopped; }
void dglabJoyconRescan(void) { ++rescans; }
bool dglabJoyconIsConnected(DglabJoyconSide side) { (void)side; return true; }
size_t dglabJoyconDescribe(DglabJoyconSide side, char* out, size_t size) { (void)side; return snprintf(out, size, "sensor"); }
size_t dglabJoyconPoll(DglabJoyconSide side, DglabMotionSample* out, size_t max, bool usable) {
    (void)side; assert(max > 0); ++motion_polled;
    if (!usable) return 0;
    memset(out, 0, sizeof(*out)); out->angular_velocity[0] = 100.0f; return 1;
}
void dglabTouchStart(void) { ++touch_started; }
size_t dglabTouchPoll(DglabTouchFrame* frame) { ++touch_polled; *frame = touch_readings; return frame->count; }
bool dglabTouchHandheld(void) { return true; }

#include "../../nro/source/pages/controller.inc"

static void tick(u64 buttons) {
    now_ns += 25000000;
    assert(appPageUpdate(buttons, buttons, now_ns));
}
static void test_logs(void) {
    for (DglabMenuResult page = DglabMenuResult_Socket; page <= DglabMenuResult_Ble; ++page) {
        appPageEnter(&service, &pad, page);
        tick(HidNpadButton_Y | HidNpadButton_Up | HidNpadButton_A);
        unsigned before = adjustments;
        tick(HidNpadButton_A | HidNpadButton_X | HidNpadButton_Up);
        assert(adjustments == before && net_starts == 0 && net_stops == 0 && ble_stops == 0);
        tick(0); tick(HidNpadButton_Down); assert(scrolls > 0);
        tick(HidNpadButton_Y | HidNpadButton_Up | HidNpadButton_A);
        assert(net_starts == 0 && preparation_starts == 0);
        before = adjustments;
        tick(HidNpadButton_Up); assert(adjustments == before); // Held direction cannot leak out of logs.
        tick(0); tick(HidNpadButton_Up); assert(adjustments == before + 1);
        const DglabGameplayState* state = appPageState();
        assert(state->page == page);
        assert(state->log.lines == (page == DglabMenuResult_Ble ? g_log_ble.pointers : g_log_net.pointers));
        tick(0); tick(HidNpadButton_Y); // B from a log also returns directly to the menu.
        assert(!appPageUpdate(HidNpadButton_B, HidNpadButton_B, now_ns));
        appPageLeave();
    }
    assert(touch_polled == 0 && motion_polled == 0);
}
static void test_ble_lifecycle(void) {
    preparation.state = DglabPocState_Initializing;
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_A);
    assert(preparation_starts == 1 && appPageState()->value.ble.starting);
    unsigned before = stops;
    assert(!appPageUpdate(HidNpadButton_B | HidNpadButton_A, HidNpadButton_B, now_ns));
    appPageLeave(); assert(stops == before + 1 && ble_starts == 0);
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_A);
    before = stops; tick(HidNpadButton_X); assert(stops == before + 1);
    assert(!appPageState()->value.ble.starting);
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_A);
    preparation.state = DglabPocState_Stopped; preparation.milestone = DGLAB_POC_MILESTONE_DRIVER_READY;
    tick(0); assert(ble_starts == 1);
    assert(last_ble_start.limit_a == 100 && last_ble_start.limit_b == 100);
    ble_status.state = DglabBleState_Connected;
    tick(0); before = ble_stops;
    appPageLeave(); appPageLeave(); assert(ble_stops == before); // Returning keeps a connected session.
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_X);
    assert(ble_stops == before + 1);
    ble_status.state = DglabBleState_Idle;
    preparation.state = DglabPocState_Initializing;
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_A);
    preparation_error = 1; before = stops; tick(0);
    assert(stops == before + 1 && appPageState()->value.ble.status.state == DglabBleState_Failed);
    appPageLeave(); assert(stops == before + 1); preparation_error = 0;
}
static void test_gameplay_cleanup(void) {
    appPageEnter(&service, &pad, DglabMenuResult_Motion);
    unsigned clear_before = clears, stop_before = sensors_stopped;
    tick(0); tick(HidNpadButton_Y); assert(motion_polled == 4 && rescans == 1 && uploads > 0);
    assert(last_slot_count > 0 && last_slots[0].strength > 0);
    appPageLeave(); appPageLeave(); assert(clears == clear_before + 1 && sensors_stopped == stop_before + 1);
    assert(!appPageUpdate(0, 0, now_ns));
    appPageEnter(&service, &pad, DglabMenuResult_Touch);
    touch_readings.count = 1;
    touch_readings.points[0] = (DglabTouchPoint){.x = 600, .y = DGLAB_TOUCH_VALUE_TOP, .finger_id = 1, .start = true};
    clear_before = clears; tick(0);
    assert(touch_polled == 1 && appPageState()->value.touch.held_a && !appPageState()->value.touch.held_b);
    unsigned before = uploads;
    assert(!appPageUpdate(HidNpadButton_B, HidNpadButton_B, now_ns));
    assert(uploads == before && clears == clear_before + 1); appPageLeave(); assert(clears == clear_before + 1);
    appPageEnter(&service, &pad, DglabMenuResult_Motion);
    clear_before = clears; stop_before = sensors_stopped;
    // Console/exception/system exit all use this same idempotent leave path.
    appPageEnter(&service, &pad, DglabMenuResult_Socket);
    assert(clears == clear_before + 1 && sensors_stopped == stop_before + 1);
    assert(touch_started == 1 && sensors_started == 2);
    appPageLeave();
}
static void test_status_and_handoff(void) {
    DglabUiKeys keys;
    dglabUiKeysReset(&keys, HidNpadButton_A | HidNpadButton_Up);
    appPageEnter(&service, &pad, DglabMenuResult_Socket);
    dglabUiKeysUpdate(&keys, HidNpadButton_A | HidNpadButton_Up);
    unsigned before = adjustments;
    assert(appPageUpdate(keys.down, keys.held, now_ns)); assert(net_starts == 0 && adjustments == before);
    dglabUiKeysUpdate(&keys, 0); dglabUiKeysUpdate(&keys, HidNpadButton_A);
    assert(appPageUpdate(keys.down, keys.held, now_ns)); assert(net_starts == 1);
    net_status.state = DglabNetState_Listening; tick(HidNpadButton_A); assert(net_stops == 1 && asleep_suppressed);
    net_status.reports_received = 1; net_status.app_limit_a = 0; net_status.app_limit_b = 12;
    g_test_strength_a = g_test_strength_b = 80; tick(0);
    assert(appPageState()->value.socket.limit_a == 0 && g_test_strength_a == 0 && g_test_strength_b == 12);
    status_failure = true; tick(0); assert(!appPageState()->value.socket.status_ok);
    assert(!asleep_suppressed); appPageLeave();
}
static void test_ble_gate_and_editor(void) {
    unsigned starts = preparation_starts, connects = ble_starts;
    patch_result = DglabBlePatch_Missing;
    appPageEnter(&service, &pad, DglabMenuResult_Ble);
    assert(!appPageUpdate(HidNpadButton_A, HidNpadButton_A, now_ns));
    assert(preparation_starts == starts && ble_starts == connects);
    patch_result = DglabBlePatch_Ok;
    ble_status.state = DglabBleState_Idle;
    appPageEnter(&service, &pad, DglabMenuResult_Ble);
    edit_result = DglabBleEdit_None;
    tick(HidNpadButton_L);
    assert(edits == 1 && appPageState()->value.ble.status.address[0] == 1);
    // Confirmation/cancel buttons sampled after keyboard must be released.
    assert(appPageUpdate(HidNpadButton_A | HidNpadButton_B,
        HidNpadButton_A | HidNpadButton_B, now_ns));
    assert(preparation_starts == starts);
    tick(0);
    edit_result = DglabBleEdit_SaveFailed;
    tick(HidNpadButton_L);
    assert(edits == 2 && appPageState()->value.ble.status.address[0] == 1);
    tick(0);
    edit_result = DglabBleEdit_Saved;
    tick(HidNpadButton_L | HidNpadButton_A);
    assert(edits == 3 && appPageState()->value.ble.status.address[0] == 0xAA);
    assert(preparation_starts == starts);
    tick(0);
    ble_status.state = DglabBleState_Connecting;
    tick(HidNpadButton_L); assert(edits == 3);
    ble_status.state = DglabBleState_Connected;
    tick(HidNpadButton_L); assert(edits == 3);
    ble_status.state = DglabBleState_Idle;
    g_page.view.ble.state.starting = true;
    preparation.state = DglabPocState_Initializing;
    tick(HidNpadButton_L); assert(edits == 3);
    g_page.view.ble.state.starting = false;
    tick(HidNpadButton_Y); tick(HidNpadButton_L); assert(edits == 3);
    appPageLeave();
}
int main(void) {
    test_logs(); test_ble_lifecycle(); test_gameplay_cleanup(); test_status_and_handoff();
    status_failure = false;
    test_ble_gate_and_editor();
    appPageEnter(&service, &pad, DglabMenuResult_Socket);
    for (unsigned i = 0; i < 600; ++i) tick(0);
    appPageLeave();
    preparation.state = DglabPocState_Initializing;
    appPageEnter(&service, &pad, DglabMenuResult_Ble); tick(HidNpadButton_A);
    unsigned stopped_before = stops, ble_stopped_before = ble_stops;
    appPageExitCleanup(&service); appPageExitCleanup(&service);
    assert(stops == stopped_before + 1 && ble_stops == ble_stopped_before + 1);
    assert(!appPageUpdate(0, 0, now_ns));
    puts("Shared page controller IPC/HID, routing, sampling and cleanup regression tests passed");
    return 0;
}
