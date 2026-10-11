// Private C controller, included by the app host and the mock-IPC regression harness.
// Both UI backends call these same enter/update/leave functions.

typedef struct {
    DglabScreenState state;
    DglabMotionFeedConfig config;
    char url[DGLAB_NET_QR_MAX];
    bool server_running, url_ok, log_open;
    int log_offset;
} SocketPage;

static void socketEnter(SocketPage* v)
{
    memset(v, 0, sizeof(*v));
    motionSettingsLoad(&v->config);
}

static void socketUpdate(SocketPage* v, Service* dglab, u64 down, u64 held, u64 now_ns)
{
    DglabNetQrChunk chunk;

    memset(&v->state, 0, sizeof(v->state));
    memset(&chunk, 0, sizeof(chunk));

    v->state.url = v->url;
    v->state.url_ok = v->url_ok;
    v->state.sysmodule_ok = appSysmoduleOk(dglab);

    v->state.status_ok =
        R_SUCCEEDED(serviceDispatchOut(dglab, DGLAB_IPC_CMD_NET_STATUS, v->state.status));

    v->state.log_lines = g_log_net.pointers;
    v->state.log_count = g_log_net.filled;

    if (R_SUCCEEDED(serviceDispatchOut(dglab, DGLAB_IPC_CMD_NET_QR, chunk))) {
        v->url_ok = true;
        snprintf(v->url, sizeof(v->url), "%s", chunk.text);
    } else {
        // Without a LAN address there is no QR code to show.
        v->url_ok = false;
        v->url[0] = '\0';
    }

    v->state.url_ok = v->url_ok;
    v->server_running = v->state.status_ok &&
        (v->state.status.state == DglabNetState_Listening ||
            v->state.status.state == DglabNetState_Paired);
    appAutoSleepFollow(v->server_running);

    // The ceiling the two strength rows show and the D-pad stops at: the
    // App's own limit once it has reported one, the parameters page's
    // channel limit until then. Both are recomputed from this frame's poll,
    // so the screen and the keys can never disagree.
    v->state.limit_a = dglabChannelCeiling(
        v->state.status_ok && v->state.status.reports_received != 0, v->state.status.app_limit_a,
        v->config.channel_limit_a);
    v->state.limit_b = dglabChannelCeiling(
        v->state.status_ok && v->state.status.reports_received != 0, v->state.status.app_limit_b,
        v->config.channel_limit_b);
    clampStrengthsToCeilings(dglab, v->state.limit_a, v->state.limit_b, true);

    if (down & HidNpadButton_Y) {
        v->log_open = !v->log_open;

        if (v->log_open)
            v->log_offset = logMaxOffset(g_log_net.filled);
    } else if (v->log_open) {
        v->log_offset = logScrollFromDirections(v->log_offset, logMaxOffset(g_log_net.filled), down,
            held, now_ns);
    } else {
        if (down & HidNpadButton_A)
            toggleServer(dglab, v->server_running);

        testChannelButtons(dglab, down);
        adjustStrengthFromDirections(dglab, down, v->state.limit_a, v->state.limit_b);
        repeatStrengthFromDirections(dglab, held, now_ns,
            v->state.limit_a, v->state.limit_b);
    }

    v->state.log_open = v->log_open;
    v->state.log_offset = v->log_offset;

    // Read after the buttons are handled, so a press shows up in the same
    // frame it happened.
    v->state.test_strength_a = g_test_strength_a;
    v->state.test_strength_b = g_test_strength_b;
    v->state.last_command = g_last_command;
    v->state.last_command_tone = g_last_command_tone;
    // Which of the two warning lines the server row carries. It follows the
    // flag this frame's poll set, which is one frame behind a press - the
    // same lag the status row above already has.
    v->state.auto_sleep_suppressed = dglabAutoSleepActive();

}

typedef struct {
    DglabMotionFeedConfig config;
    DglabMotionFeed feed_a, feed_b;
    DglabMotionScreenState state;
    u64 last_ticks;
    bool described, link_known, left_connected, right_connected;
    u32 pad_device, pad_styles, pad_attributes;
    bool pad_handheld;
} MotionPage;

static void motionEnter(MotionPage* v, Service* dglab, u64 now_ns)
{
    memset(v, 0, sizeof(*v));
    motionSettingsLoad(&v->config);
    dglabMotionFeedInit(&v->feed_a, &v->config);
    dglabMotionFeedInit(&v->feed_b, &v->config);

    memset(&v->state, 0, sizeof(v->state));
    v->state.link = dglabString(DglabString_StateNotStarted);
    v->state.last_upload = "";

    // Whatever the test buttons left queued should not play underneath the
    // motion stream.
    noteCommand(dglabString(DglabString_CmdClear), sendTestCommand(dglab, DglabNetCommand_Clear, 0, 0), NULL);

    dglabJoyconStart();

    v->last_ticks = now_ns;

}

static void motionUpdate(MotionPage* v, Service* dglab, PadState* pad, u64 down, u64 held, u64 now_ns)
{
    DglabNetWaveformSlot slots[DGLAB_NET_WAVEFORM_MAX_SLOTS];
    DglabMotionSample samples[MOTION_DRAIN_MAX];
    u32 elapsed_ns;
    u64 now;

    now = now_ns;
    elapsed_ns = (u32)(now - v->last_ticks);
    v->last_ticks = now;

    // The server state is asked for every frame, not only when the panel is
    // rebuilt: A starts and stops from what this frame saw, and the bottom
    // bar's label comes from the same pass - the key and the label cannot
    // disagree. The panel still refreshes at its own rate below.
    {
        DglabNetStatus status;
        bool status_ok;

        memset(&status, 0, sizeof(status));
        status_ok = R_SUCCEEDED(
            serviceDispatchOut(dglab, DGLAB_IPC_CMD_NET_STATUS, status));
        v->state.server_running = status_ok &&
            (status.state == DglabNetState_Listening ||
                status.state == DglabNetState_Paired);

        // The ceiling the two strength rows show and the D-pad stops at: the
        // App's own limit while it reports one, the parameters page's
        // channel limit otherwise (dglabChannelCeiling()).
        v->state.limit_a = dglabChannelCeiling(status_ok && status.reports_received != 0,
            status.app_limit_a, v->config.channel_limit_a);
        v->state.limit_b = dglabChannelCeiling(status_ok && status.reports_received != 0,
            status.app_limit_b, v->config.channel_limit_b);

        // The motion page is the other place the server can be started, so
        // it has to keep the console's sleep timer in step with it too.
        appAutoSleepFollow(v->state.server_running);

        // A BLE session drives the device locally (the sysmodule routes
        // NET_SEND / NET_WAVEFORM to it while it is active), and it outlives
        // the page that started it: while one runs, this row says so instead
        // of showing the socket server's state, which is not what the packets
        // this page sends end up driving.
        if (appBleSessionActive(dglab)) {
            v->state.link = dglabString(DglabString_BleTitle);
            v->state.link_tone = DglabCmdTone_Ok;
        } else if (!status_ok) {
            v->state.link = dglabString(DglabString_StateIpcFailed);
            v->state.link_tone = DglabCmdTone_Error;
        } else {
            v->state.link = dglabNetStateText(status.state);
            v->state.link_tone = (status.state == DglabNetState_Paired) ? DglabCmdTone_Ok
                                                                    : DglabCmdTone_Warn;
        }
    }

    // The same shortcuts as the socket page: A starts and stops the server,
    // X clears what a test left playing, ZL and ZR fire one channel each,
    // and the D-pad dials the two channel strengths (up/down A, left/right
    // B). Nothing is focused here, so the D-pad has no other job.
    if (down & HidNpadButton_A)
        toggleServer(dglab, v->state.server_running);

    // Y takes a fresh set of sensor handles. The mode's handles describe the
    // assignment the console had when they were taken, so a Joy-Con that was
    // turned off or plugged back in keeps its row on "not connected" until
    // they are taken again - this is that, without leaving the page.
    if (down & HidNpadButton_Y) {
        dglabJoyconRescan();
        logPushLine(&g_log_net, "motion rescan (Y)");
    }

    testChannelButtons(dglab, down);
    clampStrengthsToCeilings(dglab, v->state.limit_a, v->state.limit_b, true);
    adjustStrengthFromDirections(dglab, down, v->state.limit_a, v->state.limit_b);
    repeatStrengthFromDirections(dglab, held,
        now_ns, v->state.limit_a, v->state.limit_b);

    // Which sides this mode can read at all, from the pad the buttons come
    // from: the six-axis handles keep handing over readings for a Joy-Con
    // that is attached to the console or switched off, so they are not a
    // connection signal (hardware report, 2026-09-17 - and the reason the two
    // rows never went back to 未连接). `padIsHandheld` covers "plugged back
    // in", the two attribute bits cover "switched off / not there".
    {
        // The per-side answer, when the console gives one: JoyLeft/JoyRight
        // mean the controller is detached and usable as this mode's input,
        // HandheldLeft/Right mean it is clipped onto the console (moving it
        // moves the console, which is not this mode), and a bit that is clear
        // means that side is switched off or gone.
        u32 device = hidGetNpadDeviceType(HidNpadIdType_No1);
        u32 styles = padGetStyleSet(pad);
        u32 attributes = padGetAttributes(pad);
        bool handheld = padIsHandheld(pad);
        bool left_ok;
        bool right_ok;

        if (device != 0) {
            left_ok = (device & HidDeviceTypeBits_JoyLeft) != 0;
            right_ok = (device & HidDeviceTypeBits_JoyRight) != 0;
        } else if (styles != 0 || (attributes & HidNpadAttribute_IsConnected) != 0) {
            // No device type: the pad state the buttons come from still says
            // whether the console is handheld and which halves are there.
            left_ok = !handheld &&
                (styles & (HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadJoyLeft)) != 0 &&
                (attributes & HidNpadAttribute_IsLeftConnected) != 0;
            right_ok = !handheld &&
                (styles & (HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadJoyRight)) != 0 &&
                (attributes & HidNpadAttribute_IsRightConnected) != 0;
        } else {
            // Nothing is answering, not even the pad: keep the old behaviour,
            // which judges each side by its readings alone.
            left_ok = true;
            right_ok = true;
        }

        v->pad_device = device;
        v->pad_styles = styles;
        v->pad_attributes = attributes;
        v->pad_handheld = handheld;

        // Drain both sides every frame: the sensors run faster than this
        // loop, and a reading that is not collected now is gone.
        for (size_t i = 0, count = dglabJoyconPoll(DglabJoycon_Left, samples,
                 MOTION_DRAIN_MAX, left_ok); i < count; i++)
            dglabMotionFeedAddSample(&v->feed_a, &samples[i]);

        for (size_t i = 0, count = dglabJoyconPoll(DglabJoycon_Right, samples,
                 MOTION_DRAIN_MAX, right_ok); i < count; i++)
            dglabMotionFeedAddSample(&v->feed_b, &samples[i]);

    }

    // One line about the sensor handles, once per visit (the log page shows
    // it, and the whole line goes to the file on the SD card). Which handles
    // a console hands over, and which of them answer, is a hardware fact:
    // this is what makes "the row says not connected while the waveform
    // plays" answerable from a log instead of a guess.
    if (!v->described) {
        char left[160];
        char right[160];
        char line[448];

        v->described = true;
        dglabJoyconDescribe(DglabJoycon_Left, left, sizeof(left));
        dglabJoyconDescribe(DglabJoycon_Right, right, sizeof(right));
        snprintf(line, sizeof(line),
            "motion device 0x%08X styles 0x%08X attrs 0x%08X handheld %u | %s | %s",
            (unsigned)v->pad_device, (unsigned)v->pad_styles, (unsigned)v->pad_attributes,
            v->pad_handheld ? 1u : 0u, left, right);
        logPushLine(&g_log_net, line);
    }

    // A side going away or coming back is worth a line of its own: whether a
    // sleeping or removed Joy-Con really clears IsConnected is one of the
    // things only hardware can answer (docs/joycon-input.md), and it is the
    // state the row is about. Only changes are logged, so a still session
    // writes nothing.
    {
        bool left_now = dglabJoyconIsConnected(DglabJoycon_Left);
        bool right_now = dglabJoyconIsConnected(DglabJoycon_Right);

        if (v->link_known) {
            // The line carries the side's own description, so a change in the
            // log says *why* it happened: which handle answered, how long the
            // side had been quiet, whether it had just re-acquired a set.
            if (left_now != v->left_connected) {
                char detail[160];
                char line[224];

                dglabJoyconDescribe(DglabJoycon_Left, detail, sizeof(detail));
                snprintf(line, sizeof(line), "motion left %s: %s",
                    left_now ? "connected" : "disconnected", detail);
                logPushLine(&g_log_net, line);
            }

            if (right_now != v->right_connected) {
                char detail[160];
                char line[224];

                dglabJoyconDescribe(DglabJoycon_Right, detail, sizeof(detail));
                snprintf(line, sizeof(line), "motion right %s: %s",
                    right_now ? "connected" : "disconnected", detail);
                logPushLine(&g_log_net, line);
            }
        }

        v->left_connected = left_now;
        v->right_connected = right_now;
        v->link_known = true;
    }

    // A batch that is all silence is dropped instead of uploaded: that is
    // what makes a still controller cost no traffic at all.
    {
        size_t produced = dglabMotionFeedAdvance(&v->feed_a, elapsed_ns, slots,
            DGLAB_NET_WAVEFORM_MAX_SLOTS);

        if (produced > 0 && slotsHaveStrength(slots, produced))
            noteCommand(dglabString(DglabString_CmdWaveformA),
                uploadSlots(dglab, MOTION_CHANNEL_A, slots, produced), NULL);
    }

    {
        size_t produced = dglabMotionFeedAdvance(&v->feed_b, elapsed_ns, slots,
            DGLAB_NET_WAVEFORM_MAX_SLOTS);

        if (produced > 0 && slotsHaveStrength(slots, produced))
            noteCommand(dglabString(DglabString_CmdWaveformB),
                uploadSlots(dglab, MOTION_CHANNEL_B, slots, produced), NULL);
    }

    v->state.sysmodule_ok = appSysmoduleOk(dglab);
    v->state.left_connected = dglabJoyconIsConnected(DglabJoycon_Left);
    v->state.right_connected = dglabJoyconIsConnected(DglabJoycon_Right);
    v->state.moving_a = dglabMotionFeedIsMoving(&v->feed_a);
    v->state.moving_b = dglabMotionFeedIsMoving(&v->feed_b);
    v->state.level_a = (unsigned)(dglabMotionFeedLevel(&v->feed_a) * 100.0f + 0.5f);
    v->state.level_b = (unsigned)(dglabMotionFeedLevel(&v->feed_b) * 100.0f + 0.5f);
    v->state.frequency_a = dglabMotionFeedFrequencyMs(&v->feed_a);
    v->state.frequency_b = dglabMotionFeedFrequencyMs(&v->feed_b);
    v->state.channel_strength_a = g_test_strength_a;
    v->state.channel_strength_b = g_test_strength_b;
    v->state.last_upload = g_last_command;
    v->state.last_upload_tone = g_last_command_tone;

}

typedef struct {
    DglabMotionFeedConfig config;
    DglabTouchFeed touch_feed;
    DglabMotionFeed stream[DglabTouchChannelValue_Count];
    DglabTouchScreenState state;
    u64 last_ticks;
    bool described, have_last, last_held_a, last_held_b;
} TouchPage;

static void touchEnter(TouchPage* v, Service* dglab, u64 now_ns)
{
    memset(v, 0, sizeof(*v));
    // Entering the mode is what initializes the panel: nothing else in this NRO
    // touches it (nro/AGENTS.md). libnx has no error to report here - a refusal
    // is its fatal error page - which is why the log's first line is the answer
    // the probe run exists for.
    dglabTouchStart();

    // One parameter file for both modes (docs/touch-input.md): the envelope, the
    // idle stop, the two frequency ends and the strength ceiling are the numbers
    // the Advanced page edits for the motion mode. The one thing this mode decides
    // for itself is that its density is the panel's horizontal axis and not its
    // level.
    motionSettingsLoad(&v->config);
    v->config.frequency_follows_level = false;

    for (unsigned channel = 0; channel < (unsigned)DglabTouchChannelValue_Count; channel++)
        dglabMotionFeedInit(&v->stream[channel], &v->config);

    dglabTouchFeedInit(&v->touch_feed);

    memset(&v->state, 0, sizeof(v->state));
    v->state.link = dglabString(DglabString_StateNotStarted);
    v->state.last_upload = "";
    // The density switch is a parameter rather than a per-frame value: the page
    // only needs it to know whether there is a horizontal axis worth drawing.
    v->state.density_fixed = v->config.density_fixed;

    // Whatever the test buttons left queued should not play underneath the mode.
    noteCommand(dglabString(DglabString_CmdClear), sendTestCommand(dglab, DglabNetCommand_Clear, 0, 0), NULL);

    v->last_ticks = now_ns;

}

static void touchUpdate(TouchPage* v, Service* dglab, u64 down, u64 held, u64 now_ns)
{
    DglabNetWaveformSlot slots[DGLAB_NET_WAVEFORM_MAX_SLOTS];
    DglabTouchFrame readings;
    u32 elapsed_ns;
    u64 now;

    // The slots stay paced by time rather than by frames, like the motion
    // mode's: a slow frame produces several of them at once.
    now = now_ns;
    elapsed_ns = (u32)(now - v->last_ticks);
    v->last_ticks = now;

    // The same server poll and shortcuts as the socket and motion pages: A
    // starts and stops the server, X clears, ZL and ZR fire one channel, and
    // the D-pad dials the two channel strengths.
    {
        DglabNetStatus status;
        bool status_ok;

        memset(&status, 0, sizeof(status));
        status_ok = R_SUCCEEDED(
            serviceDispatchOut(dglab, DGLAB_IPC_CMD_NET_STATUS, status));
        v->state.server_running = status_ok &&
            (status.state == DglabNetState_Listening ||
                status.state == DglabNetState_Paired);

        appAutoSleepFollow(v->state.server_running);

        // The ceiling rule is the socket and motion pages' one: the App's own
        // limit while it reports one, the parameters page's otherwise.
        v->state.limit_a = dglabChannelCeiling(status_ok && status.reports_received != 0,
            status.app_limit_a, v->config.channel_limit_a);
        v->state.limit_b = dglabChannelCeiling(status_ok && status.reports_received != 0,
            status.app_limit_b, v->config.channel_limit_b);

        // Same as the motion page: while a BLE session runs, that is what the
        // touch panel's packets drive.
        if (appBleSessionActive(dglab)) {
            v->state.link = dglabString(DglabString_BleTitle);
            v->state.link_tone = DglabCmdTone_Ok;
        } else if (!status_ok) {
            v->state.link = dglabString(DglabString_StateIpcFailed);
            v->state.link_tone = DglabCmdTone_Error;
        } else {
            v->state.link = dglabNetStateText(status.state);
            v->state.link_tone = (status.state == DglabNetState_Paired) ? DglabCmdTone_Ok
                                                                    : DglabCmdTone_Warn;
        }
    }

    if (down & HidNpadButton_A)
        toggleServer(dglab, v->state.server_running);

    testChannelButtons(dglab, down);
    clampStrengthsToCeilings(dglab, v->state.limit_a, v->state.limit_b, true);
    adjustStrengthFromDirections(dglab, down, v->state.limit_a, v->state.limit_b);
    repeatStrengthFromDirections(dglab, held,
        now_ns, v->state.limit_a, v->state.limit_b);

    // The panel, then the mapping, then the slots: the same three steps the
    // motion mode takes with a sensor sample, which is what lets the two modes
    // share the envelope, the idle stop and the parameters.
    dglabTouchPoll(&readings);
    dglabTouchFeedUpdate(&v->touch_feed, &readings);

    for (unsigned channel = 0; channel < (unsigned)DglabTouchChannelValue_Count; channel++) {
        const DglabTouchChannelState* target = dglabTouchFeedChannel(&v->touch_feed, channel);

        if (target->held)
            dglabMotionFeedSetTarget(&v->stream[channel], target->level, target->density);
    }

    // An all-zero batch is not uploaded, which is the rule the motion mode
    // already works by: a finger held on the bottom rule, and everything after
    // a release has decayed, costs no traffic at all.
    for (unsigned channel = 0; channel < (unsigned)DglabTouchChannelValue_Count; channel++) {
        size_t produced = dglabMotionFeedAdvance(&v->stream[channel], elapsed_ns, slots,
            DGLAB_NET_WAVEFORM_MAX_SLOTS);

        if (produced > 0 && slotsHaveStrength(slots, produced))
            noteCommand(dglabString(touchChannelCommand(channel)),
                uploadSlots(dglab, touchChannelNumber(channel), slots, produced), NULL);
    }

    {
        const DglabTouchChannelState* a =
            dglabTouchFeedChannel(&v->touch_feed, DglabTouchChannelValue_A);
        const DglabTouchChannelState* b =
            dglabTouchFeedChannel(&v->touch_feed, DglabTouchChannelValue_B);

        v->state.held_a = a->held;
        v->state.x_a = a->x;
        v->state.y_a = a->y;
        v->state.level_a = (unsigned)(dglabMotionFeedLevel(&v->stream[DglabTouchChannelValue_A]) *
                                   100.0f + 0.5f);
        v->state.frequency_a = dglabMotionFeedFrequencyMs(&v->stream[DglabTouchChannelValue_A]);

        v->state.held_b = b->held;
        v->state.x_b = b->x;
        v->state.y_b = b->y;
        v->state.level_b = (unsigned)(dglabMotionFeedLevel(&v->stream[DglabTouchChannelValue_B]) *
                                   100.0f + 0.5f);
        v->state.frequency_b = dglabMotionFeedFrequencyMs(&v->stream[DglabTouchChannelValue_B]);
    }

    v->state.docked = !dglabTouchHandheld();

    // One line at the start and one per change of which halves have a finger:
    // the log then holds the whole session - the LIFO depth the panel answered
    // with, the coordinates, and what the mode made of them.
    if (!v->described) {
        v->described = true;
        touchLogLine("start");
    } else if (!v->have_last || v->state.held_a != v->last_held_a || v->state.held_b != v->last_held_b) {
        char tag[80];

        if (v->state.held_a && v->state.held_b) {
            snprintf(tag, sizeof(tag), "both A %u/%ums B %u/%ums", v->state.level_a,
                v->state.frequency_a, v->state.level_b, v->state.frequency_b);
        } else if (v->state.held_a) {
            snprintf(tag, sizeof(tag), "A %u/%ums", v->state.level_a, v->state.frequency_a);
        } else if (v->state.held_b) {
            snprintf(tag, sizeof(tag), "B %u/%ums", v->state.level_b, v->state.frequency_b);
        } else {
            snprintf(tag, sizeof(tag), "none");
        }

        touchLogLine(tag);
    }

    v->last_held_a = v->state.held_a;
    v->last_held_b = v->state.held_b;
    v->have_last = true;

    v->state.sysmodule_ok = appSysmoduleOk(dglab);

    v->state.channel_strength_a = g_test_strength_a;
    v->state.channel_strength_b = g_test_strength_b;
    v->state.last_upload = g_last_command;
    v->state.last_upload_tone = g_last_command_tone;
}

typedef struct {
    DglabBlePageState state;
    DglabMotionFeedConfig config;
    bool preparation_failed;
    u64 last_frame_ns, last_heartbeat_ns;
} BlePage;

static void bleEnter(BlePage* v)
{
    memset(v, 0, sizeof(*v));
    motionSettingsLoad(&v->config);
    v->state.limit_a = v->config.channel_limit_a;
    v->state.limit_b = v->config.channel_limit_b;
}

static void bleUpdate(BlePage* v, Service* dglab, PadState* pad, u64 down, u64 held, u64 now_ns)
{
    DglabBleStatus status;
    DglabPocStatus poc_status;
    Result status_rc;

    if (v->last_frame_ns && now_ns - v->last_frame_ns >= 250000000ull) {
        char line[96];
        snprintf(line, sizeof(line), "nro BLE: frame gap %llu ms",
            (unsigned long long)((now_ns - v->last_frame_ns) / 1000000ull));
        logPushLine(&g_log_ble, line);
    }
    v->last_frame_ns = now_ns;
    if (v->state.starting && now_ns - v->last_heartbeat_ns >= 1000000000ull) {
        char line[96];
        snprintf(line, sizeof(line), "nro BLE: UI alive t=%llu ms pad_connected=%u style=0x%X",
            (unsigned long long)(now_ns / 1000000ull), padIsConnected(pad) ? 1u : 0u,
            padGetStyleSet(pad));
        logPushLine(&g_log_ble, line);
        v->last_heartbeat_ns = now_ns;
    }

    if (down & HidNpadButton_Y) {
        v->state.log_open = !v->state.log_open;
        logPushLine(&g_log_ble, v->state.log_open ? "nro BLE: Y opened log" : "nro BLE: Y closed log");

        if (v->state.log_open)
            v->state.log_offset = logMaxOffset(g_log_ble.filled);
    } else if (v->state.log_open) {
        v->state.log_offset = logScrollFromDirections(v->state.log_offset,
            logMaxOffset(g_log_ble.filled), down, held, now_ns);
    } else {
        // The mixer, the same two helpers the socket and motion pages call:
        // up/down dial channel A, left/right channel B, one step per press
        // and a walk while held. A running session follows them because the
        // sysmodule routes NET_SEND to it while it is active.
        adjustStrengthFromDirections(dglab, down, v->state.limit_a, v->state.limit_b);
        repeatStrengthFromDirections(dglab, held, now_ns,
            v->state.limit_a, v->state.limit_b);

        if (v->state.status.state == DglabBleState_Connected)
            testChannelButtons(dglab, down & (HidNpadButton_ZL | HidNpadButton_ZR));

        if (down & HidNpadButton_X) {
            if (v->state.starting) {
                v->state.starting = false;
                v->state.driver_running = false;
                bleLogFailure("cancel driver preparation",
                    serviceDispatch(dglab, DGLAB_IPC_POC_CMD_STOP));
            } else {
                bleLogFailure("stop BLE", serviceDispatch(dglab, DGLAB_IPC_CMD_BLE_STOP));
            }
        }
    }

    if ((down & HidNpadButton_A) && !v->state.log_open && !v->state.starting &&
        !bleStateIsActive(v->state.status.state)) {
        u8 address[6];

        if (loadBleAddress(address)) {
            v->preparation_failed = false;
            DglabPocStartRequest request = { 0 };
            DglabPocActionRequest action = { .action = DglabPocAction_ProbeBtdrvScan };
            Result rc;

            request.flags = DGLAB_POC_START_FLAG_TARGET_ADDRESS |
                DGLAB_POC_START_FLAG_SHORT_OBSERVATION;
            memcpy(request.target_address, address, sizeof(request.target_address));
            memcpy(v->state.status.address, address, sizeof(v->state.status.address));

            u64 call_started = armGetSystemTick();
            rc = serviceDispatchIn(dglab, DGLAB_IPC_POC_CMD_START, request);
            bleLogCallTiming("DGLAB_IPC_POC_CMD_START", call_started);
            bleLogFailure("driver probe start", rc);

            if (R_SUCCEEDED(rc)) {
                u64 call_started = armGetSystemTick();
                rc = serviceDispatchIn(dglab, DGLAB_IPC_POC_CMD_ACTION, action);
                bleLogCallTiming("DGLAB_IPC_POC_CMD_ACTION", call_started);
                bleLogFailure("driver probe action", rc);
                if (R_FAILED(rc))
                    bleLogFailure("cancel driver preparation",
                        serviceDispatch(dglab, DGLAB_IPC_POC_CMD_STOP));
            }

            if (R_SUCCEEDED(rc))
                v->state.starting = true;
        }
    }

    memset(&status, 0, sizeof(status));
    u64 status_started = armGetSystemTick();
    status_rc = serviceDispatchOut(dglab, DGLAB_IPC_CMD_BLE_STATUS, status);
    bleLogCallTiming("BLE_STATUS", status_started);
    v->state.sysmodule_ok = R_SUCCEEDED(status_rc);

    if (R_SUCCEEDED(status_rc)) {
        u8 address[6];

        memcpy(address, v->state.status.address, sizeof(address));
        v->state.status = status;
        memcpy(v->state.status.address, address, sizeof(address));
    }

    // Step 1 of the start sequence: wait for the driver-level session to
    // finish, then send step 2. The PoC status is the only place that says
    // whether a session is still running.
    if (v->state.starting) {
        u8 address[6];

        memset(&poc_status, 0, sizeof(poc_status));
        u64 preparation_started = armGetSystemTick();
        Result preparation_rc = serviceDispatchOut(dglab,
            DGLAB_IPC_POC_CMD_STATUS, poc_status);
        bleLogCallTiming("POC_STATUS", preparation_started);
        v->state.driver_running = R_SUCCEEDED(preparation_rc) &&
            poc_status.state == DglabPocState_Initializing;

        if (R_FAILED(preparation_rc)) {
            v->state.starting = false;
            v->preparation_failed = true;
            bleLogFailure("driver preparation status", preparation_rc);
            bleLogFailure("cancel driver preparation",
                serviceDispatch(dglab, DGLAB_IPC_POC_CMD_STOP));
        } else if (!v->state.driver_running &&
            (poc_status.state != DglabPocState_Stopped || poc_status.last_result != 0 ||
             !(poc_status.milestone & DGLAB_POC_MILESTONE_DRIVER_READY))) {
            v->state.starting = false;
            v->preparation_failed = true;
            bleLogFailure("driver preparation incomplete", poc_status.last_result != 0 ?
                (Result)poc_status.last_result : MAKERESULT(Module_Libnx, LibnxError_BadInput));
        } else if (!v->state.driver_running && loadBleAddress(address)) {
            DglabBleStartRequest request = { 0 };
            Result rc;

            v->state.starting = false;
            request.limit_a = v->state.limit_a;
            request.limit_b = v->state.limit_b;
            memcpy(request.address, address, sizeof(request.address));
            memcpy(v->state.status.address, address, sizeof(v->state.status.address));
            u64 call_started = armGetSystemTick();
            rc = serviceDispatchIn(dglab, DGLAB_IPC_CMD_BLE_START, request);
            bleLogCallTiming("DGLAB_IPC_CMD_BLE_START", call_started);
            bleLogFailure("ble session start", rc);

            // The session is being told this pair, so that is the pair the
            // rows show and the frame loop compares against (no need to push
            // it again the moment the session reports itself as connecting).
            if (R_SUCCEEDED(rc)) {
                g_ble_limits_known = true;
                g_ble_limit_a = request.limit_a;
                g_ble_limit_b = request.limit_b;
            } else if (R_VALUE(rc) == KERNELRESULT(TimedOut)) {
                v->state.starting = true;
            } else {
                v->preparation_failed = true;
            }
        } else if (!v->state.driver_running) {
            v->state.starting = false;
            v->preparation_failed = true;
            bleLogFailure("driver preparation address missing",
                MAKERESULT(Module_Libnx, LibnxError_BadInput));
        }
    }

    if (v->preparation_failed)
        v->state.status.state = DglabBleState_Failed;

    // Which ceilings the rows show, and what a ceiling changed in the
    // parameters page does to a session that is already running: the pair is
    // pushed into it (BLE_LIMIT), so "the settings page says 30" and "the
    // device is capped at 30" stay the same statement. With nothing running
    // the pair is simply the file.
    if (bleStateIsActive(v->state.status.state)) {
        if (!g_ble_limits_known) {
            g_ble_limits_known = true;
            g_ble_limit_a = v->config.channel_limit_a;
            g_ble_limit_b = v->config.channel_limit_b;
        } else if (g_ble_limit_a != v->config.channel_limit_a ||
            g_ble_limit_b != v->config.channel_limit_b) {
            blePushLimits(dglab, v->config.channel_limit_a, v->config.channel_limit_b);
        }
    } else {
        g_ble_limits_known = false;
    }

    v->state.limit_a = g_ble_limits_known ? g_ble_limit_a : v->config.channel_limit_a;
    v->state.limit_b = g_ble_limits_known ? g_ble_limit_b : v->config.channel_limit_b;

    // The two rows the D-pad dials come from the same pair the socket and
    // motion pages show, so the number on screen and the number that went out
    // cannot be two different things. The page does not scroll (up and down
    // are channel A here), so there is no offset to clamp.
    clampStrengthsToCeilings(dglab, v->state.limit_a, v->state.limit_b, false);
    v->state.strength_a = g_test_strength_a;
    v->state.strength_b = g_test_strength_b;

}

// One active page, backed by app-owned storage rather than a render callback's stack.
static struct {
    Service* service;
    PadState* pad;
    DglabMenuResult page;
    u32 frames;
    u64 blocked;
    u64 last_update_ns;
    u64 intervals[240];
    size_t interval_count;
    union {
        SocketPage socket;
        MotionPage motion;
        TouchPage touch;
        BlePage ble;
    } view;
    DglabGameplayState snapshot;
} g_page;

static int appPageCompareTime(const void* a, const void* b)
{
    u64 left = *(const u64*)a, right = *(const u64*)b;
    return (left > right) - (left < right);
}

static void appPageReportTimes(DglabMenuResult page, const char* kind, u64* samples, size_t count)
{
    if (!count) return;
    qsort(samples, count, sizeof(u64), appPageCompareTime);
    char line[160];
    snprintf(line, sizeof(line), "ui page=%u %s samples=%u p95_ns=%llu max_ns=%llu",
        (unsigned)page, kind, (unsigned)count,
        (unsigned long long)samples[(count - 1) * 95 / 100],
        (unsigned long long)samples[count - 1]);
    logPushLine(&g_log_net, line);
}

static void appPageUpdateMetrics(void)
{
    appPageReportTimes(g_page.page, "updates", g_page.intervals, g_page.interval_count);
    g_page.interval_count = 0;
}

static void appPageLeave(void)
{
    DglabMenuResult page = g_page.page;
    if (page == DglabMenuResult_Exit) return;
    u64 started = armGetSystemTick();
    appPageUpdateMetrics();
    g_page.page = DglabMenuResult_Exit; // Make cleanup idempotent, including exception paths.
    if (page == DglabMenuResult_Motion)
        dglabJoyconStop();
    if (page == DglabMenuResult_Motion || page == DglabMenuResult_Touch)
        noteCommand(dglabString(DglabString_CmdClear),
            sendTestCommand(g_page.service, DglabNetCommand_Clear, 0, 0), NULL);
    if (page == DglabMenuResult_Ble && g_page.view.ble.state.starting) {
        g_page.view.ble.state.starting = false;
        bleLogFailure("cancel driver preparation",
            serviceDispatch(g_page.service, DGLAB_IPC_POC_CMD_STOP));
    }
    g_strength_hold_started_ns = g_strength_last_repeat_ns = 0;
    g_log_hold_started_ns = g_log_last_repeat_ns = 0;
    appPageTrace("leave", page, armTicksToNs(armGetSystemTick() - started));
}

static void appPageEnter(Service* service, PadState* pad, DglabMenuResult page)
{
    appPageLeave();
    memset(&g_page, 0, sizeof(g_page));
    g_page.service = service;
    g_page.pad = pad;
    g_page.page = page;
    u64 now_ns = armTicksToNs(armGetSystemTick());
    u64 started = armGetSystemTick();
    switch (page) {
        case DglabMenuResult_Socket: socketEnter(&g_page.view.socket); break;
        case DglabMenuResult_Ble: bleEnter(&g_page.view.ble); break;
        case DglabMenuResult_Motion: motionEnter(&g_page.view.motion, service, now_ns); break;
        case DglabMenuResult_Touch: touchEnter(&g_page.view.touch, service, now_ns); break;
        default: g_page.page = DglabMenuResult_Exit; break;
    }
    if (g_page.page != DglabMenuResult_Exit)
        appPageTrace("enter", page, armTicksToNs(armGetSystemTick() - started));
}

static bool g_page_exit_cleaned;
static void appPageExitCleanup(Service* service)
{
    appPageLeave();
    if (g_page_exit_cleaned) return;
    g_page_exit_cleaned = true;
    // A connected session survives ordinary pages, but never a final exit.
    serviceDispatch(service, DGLAB_IPC_CMD_BLE_STOP);
}

static bool appPageUpdate(u64 down, u64 held, u64 now_ns)
{
    if (g_page.page == DglabMenuResult_Exit) return false;
    if (g_page.last_update_ns) {
        g_page.intervals[g_page.interval_count++] = now_ns - g_page.last_update_ns;
        if (g_page.interval_count == sizeof(g_page.intervals) / sizeof(g_page.intervals[0]))
            appPageUpdateMetrics();
    }
    g_page.last_update_ns = now_ns;
    // A return press wins over other actions and over another waveform upload.
    if (down & HidNpadButton_B) {
        appPageLeave();
        return false;
    }
    g_page.blocked &= held;
    down &= ~g_page.blocked;
    held &= ~g_page.blocked;
    if ((g_page.page == DglabMenuResult_Socket || g_page.page == DglabMenuResult_Ble) &&
        (down & HidNpadButton_Y)) {
        g_page.blocked = held;
        g_strength_hold_started_ns = g_strength_last_repeat_ns = 0;
        g_log_hold_started_ns = g_log_last_repeat_ns = 0;
        down &= HidNpadButton_Y;
        held = 0;
    }
    Service* service = g_page.service;
    if (g_page.frames++ % LOG_POLL_INTERVAL_FRAMES == 0) {
        logPoll(service);
        if (g_page.page != DglabMenuResult_Ble) bleLogPoll(service);
    }
    if (g_page.page == DglabMenuResult_Ble) bleLogPoll(service);
    buildLogPointers(&g_log_net);
    buildLogPointers(&g_log_ble);
    switch (g_page.page) {
        case DglabMenuResult_Socket:
            socketUpdate(&g_page.view.socket, service, down, held, now_ns); break;
        case DglabMenuResult_Ble:
            bleUpdate(&g_page.view.ble, service, g_page.pad, down, held, now_ns); break;
        case DglabMenuResult_Motion:
            motionUpdate(&g_page.view.motion, service, g_page.pad, down, held, now_ns); break;
        case DglabMenuResult_Touch:
            touchUpdate(&g_page.view.touch, service, down, held, now_ns); break;
        default: return false;
    }
    return true;
}

static const DglabGameplayState* appPageState(void)
{
    DglabGameplayState* out = &g_page.snapshot;
    memset(out, 0, sizeof(*out));
    out->page = g_page.page;
    DglabLogRing* ring = &g_log_net;
    switch (g_page.page) {
        case DglabMenuResult_Socket:
            out->value.socket = g_page.view.socket.state;
            out->log.offset = g_page.view.socket.log_offset;
            out->log.sysmodule_ok = out->value.socket.sysmodule_ok;
            out->log.title = dglabString(DglabString_LogTitle);
            break;
        case DglabMenuResult_Ble:
            out->value.ble = g_page.view.ble.state;
            out->log.offset = g_page.view.ble.state.log_offset;
            out->log.sysmodule_ok = out->value.ble.sysmodule_ok;
            out->log.title = dglabString(DglabString_BleLogTitle);
            ring = &g_log_ble;
            break;
        case DglabMenuResult_Motion: out->value.motion = g_page.view.motion.state; break;
        case DglabMenuResult_Touch: out->value.touch = g_page.view.touch.state; break;
        default: break;
    }
    buildLogPointers(ring);
    out->log.lines = ring->pointers;
    out->log.count = ring->filled;
    out->log_generation = ring->generation;
    return out;
}

#ifndef DGLAB_UI_BOREALIS
static void runGameplayView(Service* service, PadState* pad, DglabMenuResult page)
{
    DglabUiKeys keys;
    padUpdate(pad);
    dglabUiKeysReset(&keys, padGetButtons(pad));
    appPageEnter(service, pad, page);
    DglabGameplayState drawn = {0};
    DglabScreenSnapshot socket_drawn = {0};
    bool have_drawn = false;
    u32 frames = 0, generation = 0;
    u64 frame_times[240];
    size_t frame_count = 0;
    while (appletMainLoop()) {
        u64 frame_started = armGetSystemTick();
        padUpdate(pad);
        dglabUiKeysUpdate(&keys, padGetButtons(pad));
        if (!appPageUpdate(keys.down, keys.held, armTicksToNs(armGetSystemTick()))) break;
        const DglabGameplayState* state = appPageState();
        DglabScreenSnapshot candidate = {0};
        bool changed = !have_drawn || generation != g_display_generation;
        if (page == DglabMenuResult_Socket) {
            snapshotFromState(&candidate, &state->value.socket);
            changed |= memcmp(&candidate, &socket_drawn, sizeof(candidate)) != 0;
        } else if (page == DglabMenuResult_Ble) {
            changed |= memcmp(state, &drawn, sizeof(*state)) != 0;
        } else {
            changed |= frames % (page == DglabMenuResult_Motion ? MOTION_DISPLAY_FRAMES : TOUCH_DISPLAY_FRAMES) == 0;
        }
        ++frames;
        DglabCanvas canvas;
        if (changed && dglabFramebufferBegin(&canvas)) {
            switch (page) {
                case DglabMenuResult_Socket: dglabScreenDraw(&canvas, &g_fonts, &state->value.socket); break;
                case DglabMenuResult_Ble:
                    if (state->value.ble.log_open) dglabLogPageDraw(&canvas, &g_fonts, &state->log);
                    else dglabBleDraw(&canvas, &g_fonts, &state->value.ble);
                    break;
                case DglabMenuResult_Motion: dglabMotionScreenDraw(&canvas, &g_fonts, &state->value.motion); break;
                case DglabMenuResult_Touch: dglabTouchScreenDraw(&canvas, &g_fonts, &state->value.touch); break;
                default: break;
            }
            dglabFramebufferEnd();
            drawn = *state; socket_drawn = candidate;
            generation = g_display_generation; have_drawn = true;
        }
        frame_times[frame_count++] = armTicksToNs(armGetSystemTick() - frame_started);
        if (frame_count == sizeof(frame_times) / sizeof(frame_times[0])) {
            appPageReportTimes(page, "frames", frame_times, frame_count);
            frame_count = 0;
        }
    }
    appPageReportTimes(page, "frames", frame_times, frame_count);
    appPageLeave();
}
#endif
