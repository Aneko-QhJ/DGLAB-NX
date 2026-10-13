#include "gameplay.hpp"
extern "C" {
#include <dglab/ui/strings.h>
#include <dglab/ui/theme.h>
#include <dglab/ui/page.h>
#include <dglab/ui/presentation.h>
#include <dglab/ui/qr.h>
}
#include <borealis/core/application.hpp>
#include <borealis/core/font.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/hint.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {
NVGcolor rgba(uint32_t value) {
    return nvgRGBA(value >> 24, (value >> 16) & 255, (value >> 8) & 255, value & 255);
}
NVGcolor toneColor(u32 tone) {
    if (tone == DglabCmdTone_Error) return rgba(dglabThemeGet()->error);
    if (tone == DglabCmdTone_Warn) return rgba(dglabThemeGet()->warn);
    return brls::Application::getTheme().getColor("brls/text");
}
const char* str(DglabString key) { return dglabString(key); }
void setText(brls::Label* label, const std::string& value) {
    if (label->getFullText() != value) label->setText(value);
}
std::string strength(unsigned value, unsigned limit) {
    return std::to_string(value) + "/" + std::to_string(limit);
}
class ClippedLabel : public brls::Label {
    std::string measured;
    float measuredWidth = -1;
    unsigned measuredPixels = 0;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override {
        // Text can change without changing the Yoga frame. Recompute ellipsis
        // with the actual width, and keep ink inside this row's value column.
        auto value = getFullText();
        if (isSingleLine() && (measured != value || measuredWidth != width ||
            measuredPixels != brls::Application::windowWidth)) {
            nvgFontFaceId(vg, getFont()); nvgFontSize(vg, getFontSize());
            nvgFontQuality(vg, getFontQuality());
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            float bounds[4];
            nvgTextBounds(vg, 0, 0, "\u2026", nullptr, bounds);
            setEllipsisWidth(bounds[2] - bounds[0]);
            setRequiredWidth(nvgTextBounds(vg, 0, 0, value.c_str(), nullptr, nullptr));
            onLayout(); measured = value; measuredWidth = width;
            measuredPixels = brls::Application::windowWidth;
        }
        nvgIntersectScissor(vg, x, y, width, height);
        brls::Label::draw(vg, x, y, width, height, style, ctx);
    }
};
brls::Label* text(const std::string& value, float size = DGLAB_TEXT_BODY) {
    auto* label = new ClippedLabel();
    label->setText(value); label->setFontSize(size);
    label->setSingleLine(true); label->setAutoAnimate(false);
    label->setVerticalAlign(brls::VerticalAlign::CENTER);
    return label;
}
struct InfoRow {
    brls::Box* box;
    brls::Label* value;
    std::string previous;
    InfoRow(brls::Box* parent, const std::string& title) {
        box = new brls::Box(brls::Axis::ROW);
        box->setHeight(54); box->setPadding(0, 12, 0, 12);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setLineBottom(1);
        box->setLineColor(brls::Application::getTheme().getColor("brls/sidebar/separator"));
        auto* name = text(title); name->setWidthPercentage(42); box->addView(name);
        value = text("", DGLAB_TEXT_VALUE); value->setGrow(1); box->addView(value);
        parent->addView(box);
    }
    void set(const std::string& next, u32 tone = DglabCmdTone_Ok) {
        if (previous != next) { value->setText(next); previous = next; }
        value->setTextColor(toneColor(tone));
    }
};

// This control never creates a texture. It draws sharp module rectangles directly
// in drawable pixels, without retaining GPU IDs across a console handoff.
class QrView : public brls::View {
    DglabQrCode code{};
    std::string url;
    bool valid = false, listening = false;
public:
    QrView() { setHeight(300); setId("dglab/qr"); }
    void update(const DglabScreenState& state) {
        listening = state.status_ok && (state.status.state == DglabNetState_Listening ||
            state.status.state == DglabNetState_Paired);
        std::string next = state.url_ok && state.url ? state.url : "";
        if (next != url) {
            url = next;
            valid = !url.empty() && dglabQrEncodeString(&code, url.c_str(), DglabQrEcc_M);
        }
    }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style, brls::FrameContext*) override {
        if (!listening || !valid) {
            nvgFontFaceId(vg, brls::Application::getDefaultFont());
            nvgFontSize(vg, DGLAB_TEXT_NOTE); nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, brls::Application::getTheme().getColor("brls/text_disabled"));
            const char* message = !listening ? str(DglabString_QrNotRunning) :
                (url.empty() ? str(DglabString_NoAddress) : str(DglabString_QrTooLong));
            nvgTextBox(vg, x, y + 16, width, message, nullptr);
            return;
        }
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] <= 0 || viewport[3] <= 0) return;
        float transform[6], left, top, right, bottom;
        nvgCurrentTransform(vg, transform);
        nvgTransformPoint(&left, &top, transform, x, y);
        nvgTransformPoint(&right, &bottom, transform, x + width, y + height);
        float sx = float(viewport[2]) / brls::Application::windowWidth;
        float sy = float(viewport[3]) / brls::Application::windowHeight;
        float scale = std::min(transform[0] * sx, transform[3] * sy);
        DglabQrPixels layout{};
        if (!dglabQrPixels(left * sx, top * sy, (right - left) * sx, (bottom - top) * sy,
            code.size, int(std::floor(8 * scale)), std::max(1, int(std::floor(4 * scale))), &layout)) return;
        nvgSave(vg);
        nvgResetTransform(vg); nvgScale(vg, 1 / sx, 1 / sy);
        nvgShapeAntiAlias(vg, 0);
        nvgBeginPath(vg); nvgRect(vg, layout.x, layout.y, layout.side, layout.side);
        nvgFillColor(vg, rgba(dglabThemeGet()->white)); nvgFill(vg);
        nvgBeginPath(vg);
        for (unsigned row = 0; row < code.size; ++row)
            for (unsigned col = 0; col < code.size; ++col)
                if (code.modules[row][col]) nvgRect(vg,
                    layout.x + (col + 4) * layout.module, layout.y + (row + 4) * layout.module,
                    layout.module, layout.module);
        nvgFillColor(vg, rgba(dglabThemeGet()->black)); nvgFill(vg);
        nvgRestore(vg);
    }
};

class TouchField : public brls::Box {
public:
    DglabTouchScreenState state{};
    TouchField() : brls::Box(brls::Axis::COLUMN) { setPadding(16, 220, 16, 220); }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override {
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, width, height);
        if (!state.docked) {
            auto line = [&](float x1, float y1, float x2, float y2, NVGcolor color) {
                nvgBeginPath(vg); nvgMoveTo(vg, x1, y1); nvgLineTo(vg, x2, y2);
                nvgStrokeWidth(vg, 1); nvgStrokeColor(vg, color); nvgStroke(vg);
            };
            const float top = y + DGLAB_TOUCH_VALUE_TOP - DGLAB_PAGE_CLIP_TOP;
            const float bottom = y + DGLAB_TOUCH_VALUE_BOTTOM - DGLAB_PAGE_CLIP_TOP;
            auto muted = ctx->theme.getColor("brls/text_disabled");
            auto rule = ctx->theme.getColor("brls/applet_frame/separator");
            for (int step = 1; step < 4; ++step)
                line(x + DGLAB_TOUCH_DENSITY_LEFT, top + (bottom - top) * step / 4,
                    x + DGLAB_TOUCH_DENSITY_RIGHT, top + (bottom - top) * step / 4, muted);
            if (!state.density_fixed) {
                for (unsigned probe : {0u, unsigned(DGLAB_TOUCH_SPLIT)}) {
                    float start = x + dglabTouchDensityStart(probe), end = x + dglabTouchDensityEnd(probe);
                    line(start, y, start, y + height, rule);
                    line(end, y, end, y + height, rule);
                    for (int step = 1; step < 4; ++step)
                        line(start + (end - start) * step / 4, y,
                            start + (end - start) * step / 4, y + height, muted);
                }
            }
            line(x + DGLAB_TOUCH_SPLIT, y, x + DGLAB_TOUCH_SPLIT, y + height, rule);
            auto marker = [&](bool held, unsigned px, unsigned py) {
                if (!held) return;
                float cx = x + std::clamp(float(px), float(DGLAB_TOUCH_DENSITY_LEFT + 22),
                    float(DGLAB_TOUCH_DENSITY_RIGHT - 22));
                float cy = y + std::clamp(float(py), float(DGLAB_PAGE_CLIP_TOP + 22),
                    float(DGLAB_PAGE_BAR_Y - 1 - 22)) - DGLAB_PAGE_CLIP_TOP;
                auto accent = ctx->theme.getColor("brls/accent");
                nvgBeginPath(vg); nvgCircle(vg, cx, cy, 12); nvgFillColor(vg, accent); nvgFill(vg);
                nvgBeginPath(vg); nvgCircle(vg, cx, cy, 22); nvgStrokeWidth(vg, 2);
                nvgStrokeColor(vg, accent); nvgStroke(vg);
            };
            marker(state.held_a, state.x_a, state.y_a);
            marker(state.held_b, state.x_b, state.y_b);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        nvgRestore(vg);
    }
};

// Nintendo symbols are loaded by the project FontLoader, including paired hints.
class AdjustHints : public brls::View {
public:
    AdjustHints() { setHeight(32); }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style, brls::FrameContext* ctx) override {
        nvgIntersectScissor(vg, x, y, width, height);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, ctx->theme.getColor("brls/text"));
        const std::pair<std::string, DglabString> items[] = {
            {brls::Hint::getKeyIcon(brls::BUTTON_UP, true) + brls::Hint::getKeyIcon(brls::BUTTON_DOWN, true), DglabString_HintAdjustA},
            {brls::Hint::getKeyIcon(brls::BUTTON_LEFT, true) + brls::Hint::getKeyIcon(brls::BUTTON_RIGHT, true), DglabString_HintAdjustB},
            {brls::Hint::getKeyIcon(brls::BUTTON_LT, true), DglabString_CmdTestA},
            {brls::Hint::getKeyIcon(brls::BUTTON_RT, true), DglabString_CmdTestB},
        };
        for (const auto& item : items) {
            nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_SWITCH_ICONS));
            nvgFontSize(vg, DGLAB_TEXT_ICON);
            x = nvgText(vg, x, y + height / 2, item.first.c_str(), nullptr) + 12;
            nvgFontFaceId(vg, brls::Application::getDefaultFont()); nvgFontSize(vg, DGLAB_TEXT_NOTE);
            x = nvgText(vg, x, y + height / 2, str(item.second), nullptr) + 20;
        }
    }
};
}

namespace dglab {
struct Gameplay::Impl {
    DglabMenuResult page;
    brls::AppletFrame* frame;
    brls::Box* body;
    brls::Box* info;
    brls::ScrollingFrame* logScroll;
    brls::Box* logContent;
    brls::Label* status;
    brls::Label* warning = nullptr;
    brls::Label* docked = nullptr;
    std::vector<std::unique_ptr<InfoRow>> rows;
    std::array<brls::Label*, DGLAB_SCREEN_LOG_LINES> logLabels{};
    std::array<std::string, DGLAB_SCREEN_LOG_LINES> logText{};
    std::array<brls::Label*, 3> counters{};
    QrView* qr = nullptr;
    TouchField* touch = nullptr;
    bool logOpen = false;
    bool haveLog = false;
    u32 logGeneration = 0;
    std::vector<brls::ActionIdentifier> hints;
    std::string startHint;
    bool editHint = false;

    explicit Impl(DglabMenuResult kind) : page(kind) {
        body = new brls::Box(brls::Axis::COLUMN);
        body->setFocusable(true); body->setHideHighlight(true);
        if (page == DglabMenuResult_Touch) { touch = new TouchField(); info = touch; }
        else { info = new brls::Box(page == DglabMenuResult_Socket ? brls::Axis::ROW : brls::Axis::COLUMN);
            info->setPadding(16, 54, 16, 54); }
        info->setGrow(1); body->addView(info);
        frame = new brls::AppletFrame(body);
        std::vector<brls::ActionIdentifier> old;
        for (auto action : frame->getActions()) old.push_back(action->getIdentifier());
        for (auto id : old) frame->unregisterAction(id);
        status = text("", DGLAB_TEXT_VALUE); frame->getHeader()->addView(status);
        // Default framework header/footer dimensions match the established touch axes.
        frame->getHeader()->setHeight(DGLAB_PAGE_CLIP_TOP);
        frame->getFooter()->setHeight(DGLAB_PAGE_HEIGHT - DGLAB_PAGE_BAR_Y);
        std::function<void(brls::View*)> configureHints = [&](brls::View* view) {
            if (auto* hints = dynamic_cast<brls::Hints*>(view)) hints->setAddUnableAButtonAction(false);
            if (auto* box = dynamic_cast<brls::Box*>(view))
                for (auto* child : box->getChildren()) configureHints(child);
        };
        configureHints(frame->getFooter());
        DglabString title = page == DglabMenuResult_Socket ? DglabString_SocketTitle :
            page == DglabMenuResult_Ble ? DglabString_BleTitle :
            page == DglabMenuResult_Motion ? DglabString_MotionTitle : DglabString_TouchTitle;
        frame->setTitle(str(title));
        brls::Box* details = info;
        if (page == DglabMenuResult_Socket) {
            auto* left = new brls::Box(brls::Axis::COLUMN); left->setWidth(350); left->setMarginRight(32);
            left->addView(text(str(DglabString_QrHint), DGLAB_TEXT_NOTE));
            qr = new QrView(); left->addView(qr);
            for (auto& counter : counters) { counter = text("", DGLAB_TEXT_NOTE); counter->setHeight(37); left->addView(counter); }
            info->addView(left);
            details = new brls::Box(brls::Axis::COLUMN); details->setGrow(1); info->addView(details);
        }
        details->addView(new AdjustHints());
        auto add = [&](const std::string& name) { rows.emplace_back(std::make_unique<InfoRow>(details, name)); };
        if (page == DglabMenuResult_Socket) {
            for (auto key : {DglabString_RowServer, DglabString_LabelAddress, DglabString_LabelAppId,
                DglabString_LabelChannelA, DglabString_LabelChannelB, DglabString_CommandLabel}) add(str(key));
            warning = text("", DGLAB_TEXT_NOTE); warning->setSingleLine(false); warning->setHeight(64);
            details->addView(warning);
        } else if (page == DglabMenuResult_Ble) {
            add(str(DglabString_BleState)); add(str(DglabString_BleDevice));
            add(std::string(str(DglabString_MotionVolume)) + " A");
            add(std::string(str(DglabString_MotionVolume)) + " B"); add(str(DglabString_BlePackets));
            warning = text("", DGLAB_TEXT_NOTE); warning->setSingleLine(false); warning->setHeight(64);
            details->addView(warning);
        } else {
            add(str(DglabString_MotionLink));
            add(str(page == DglabMenuResult_Motion ? DglabString_MotionJoyConLeft : DglabString_TouchHalfLeft));
            add(str(page == DglabMenuResult_Motion ? DglabString_MotionJoyConRight : DglabString_TouchHalfRight));
            add(std::string(str(DglabString_MotionVolume)) + " A");
            add(std::string(str(DglabString_MotionVolume)) + " B");
            add(str(DglabString_CommandLabel));
            if (touch) {
                docked = text(str(DglabString_TouchDocked), DGLAB_TEXT_TITLE);
                docked->setSingleLine(false); docked->setHeight(96); docked->setMarginTop(16);
                details->addView(docked);
            }
        }
        logScroll = new brls::ScrollingFrame(); logScroll->setGrow(1);
        logScroll->setMargins(DGLAB_PAGE_CONTENT_TOP - DGLAB_PAGE_CLIP_TOP, 80, 0, 80);
        logScroll->setHideHighlight(true);
        logContent = new brls::Box(brls::Axis::COLUMN); logContent->setHideHighlight(true);
        for (auto& line : logLabels) { line = text(""); line->setHeight(DGLAB_SCREEN_LOG_PITCH); logContent->addView(line); }
        logScroll->setContentView(logContent); logScroll->setVisibility(brls::Visibility::GONE);
        body->addView(logScroll);
        updateHints(false, false);
    }
    void hint(DglabString key, brls::ControllerButton button, bool hidden = false) {
        hints.push_back(frame->registerAction(str(key), button, [](brls::View*) { return true; }, hidden));
    }
    void updateHints(bool logs, bool running, bool editable = false) {
        for (auto id : hints) frame->unregisterAction(id);
        hints.clear();
        editHint = editable;
        hint(DglabString_ActionBack, brls::BUTTON_B);
        startHint = str(running ? DglabString_ActionStop : DglabString_ActionStart);
        if (logs) {
            hint(DglabString_ActionLog, brls::BUTTON_Y);
            brls::Application::getGlobalHintsUpdateEvent()->fire();
            return;
        }
        if (page == DglabMenuResult_Socket || page == DglabMenuResult_Ble) hint(DglabString_ActionLog, brls::BUTTON_Y);
        if (page == DglabMenuResult_Ble && editable) hint(DglabString_BleEditMac, brls::BUTTON_LB);
        if (page == DglabMenuResult_Motion) hint(DglabString_ActionRescan, brls::BUTTON_Y);
        hint(page == DglabMenuResult_Ble ? DglabString_ActionStop : DglabString_ActionClear, brls::BUTTON_X);
        hint(running ? DglabString_ActionStop : DglabString_ActionStart, brls::BUTTON_A);
        startHint = str(running ? DglabString_ActionStop : DglabString_ActionStart);
        brls::Application::getGlobalHintsUpdateEvent()->fire();
    }
    void update(const DglabGameplayState& state) {
        bool live = false, logs = false, running = false, editable = false;
        const char* title = nullptr;
        if (page == DglabMenuResult_Socket) {
            const auto& s = state.value.socket;
            live = s.sysmodule_ok; logs = s.log_open;
            running = s.status_ok && (s.status.state == DglabNetState_Listening || s.status.state == DglabNetState_Paired);
            title = str(DglabString_SocketTitle); qr->update(s);
            rows[0]->set(s.status_ok ? dglabNetStateText(s.status.state) : str(DglabString_StateIpcFailed),
                !s.status_ok || s.status.state == DglabNetState_Failed ? DglabCmdTone_Error : DglabCmdTone_Ok);
            char address[64]; std::snprintf(address, sizeof(address), "%s:%u", s.status.ip_text, s.status.port);
            rows[1]->set(s.status.ip_text[0] ? address : str(DglabString_NoAddress));
            rows[2]->set(s.status.peer_id[0] ? reinterpret_cast<const char*>(s.status.peer_id) : "-");
            rows[3]->set(strength(s.test_strength_a, s.limit_a)); rows[4]->set(strength(s.test_strength_b, s.limit_b));
            rows[5]->set(s.last_command && *s.last_command ? s.last_command : "-", s.last_command_tone);
            setText(warning, running ? str(s.auto_sleep_suppressed ? DglabString_SleepWarningAutoOff : DglabString_SleepWarning) : "");
            warning->setTextColor(toneColor(DglabCmdTone_Warn));
            char value[192];
            std::snprintf(value, sizeof(value), "%s %u/%u  %u/%u", str(DglabString_LabelCounters),
                s.status.clients, s.status.sessions, s.status.messages_in, s.status.messages_out); setText(counters[0], value);
            std::snprintf(value, sizeof(value), "%s %u", str(DglabString_LabelHeartbeats), s.status.heartbeats_sent); setText(counters[1], value);
            if (s.status.reports_received) std::snprintf(value, sizeof(value), "%s A %u/%u B %u/%u",
                str(DglabString_LabelAppReport), s.status.app_strength_a, s.status.app_limit_a,
                s.status.app_strength_b, s.status.app_limit_b);
            else std::snprintf(value, sizeof(value), "%s", str(DglabString_NoReport));
            setText(counters[2], value);
        } else if (page == DglabMenuResult_Ble) {
            const auto& s = state.value.ble; live = s.sysmodule_ok; logs = s.log_open; title = str(DglabString_BleTitle);
            DglabString key = s.status.state == DglabBleState_Connected ? DglabString_BleStateConnected :
                s.starting || s.status.state == DglabBleState_Connecting ? DglabString_BleStateConnecting :
                s.status.state == DglabBleState_Failed ? DglabString_BleStateFailed : DglabString_BleStateIdle;
            rows[0]->set(str(key), s.status.state == DglabBleState_Failed ? DglabCmdTone_Error : DglabCmdTone_Ok);
            char address[32]; auto* a = s.status.address;
            std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
            rows[1]->set(s.address_valid ? address : str(DglabString_BleDeviceNone));
            editable = !s.starting && s.status.state != DglabBleState_Connecting && s.status.state != DglabBleState_Connected;
            setText(warning, dglabBleEditMessage(s.edit_result));
            warning->setTextColor(toneColor(s.edit_result > DglabBleEdit_Saved ? DglabCmdTone_Error : DglabCmdTone_Warn));
            rows[2]->set(strength(s.strength_a, s.limit_a)); rows[3]->set(strength(s.strength_b, s.limit_b));
            rows[4]->set(std::to_string(s.status.packets));
        } else if (page == DglabMenuResult_Motion) {
            const auto& s = state.value.motion; live = s.sysmodule_ok; running = s.server_running; title = str(DglabString_MotionTitle);
            rows[0]->set(s.link ? s.link : "-", s.link_tone);
            auto channel = [](bool connected, bool moving, unsigned level, unsigned frequency) {
                if (!connected) return std::string(str(DglabString_MotionNotConnected));
                return std::string(str(moving ? DglabString_MotionMoving : DglabString_MotionStill)) + "  " +
                    str(DglabString_MotionLevel) + " " + std::to_string(level) + "  " + std::to_string(frequency) + "ms";
            };
            rows[1]->set(channel(s.left_connected, s.moving_a, s.level_a, s.frequency_a));
            rows[2]->set(channel(s.right_connected, s.moving_b, s.level_b, s.frequency_b));
            rows[3]->set(strength(s.channel_strength_a, s.limit_a)); rows[4]->set(strength(s.channel_strength_b, s.limit_b));
            rows[5]->set(s.last_upload && *s.last_upload ? s.last_upload : "-", s.last_upload_tone);
        } else {
            const auto& s = state.value.touch; live = s.sysmodule_ok; running = s.server_running; title = str(DglabString_TouchTitle);
            touch->state = s; // Pointer fields are never read by TouchField::draw.
            rows[0]->set(s.link ? s.link : "-", s.link_tone);
            auto channel = [](bool held, unsigned level, unsigned frequency) {
                if (!held) return std::string(str(DglabString_TouchNotTouched));
                return std::string(str(DglabString_MotionLevel)) + " " + std::to_string(level) + "  " +
                    str(DglabString_TouchDensity) + " " + std::to_string(frequency) + "ms";
            };
            rows[1]->set(channel(s.held_a, s.level_a, s.frequency_a)); rows[2]->set(channel(s.held_b, s.level_b, s.frequency_b));
            rows[3]->set(strength(s.channel_strength_a, s.limit_a)); rows[4]->set(strength(s.channel_strength_b, s.limit_b));
            rows[5]->set(s.last_upload && *s.last_upload ? s.last_upload : "-", s.last_upload_tone);
            auto visibility = s.docked ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
            if (docked->getVisibility() != visibility) docked->setVisibility(visibility);
        }
        setText(status, str(live ? DglabString_SysmoduleOk : DglabString_SysmoduleDown));
        if (logs != logOpen || editHint != editable || startHint != str(running ? DglabString_ActionStop : DglabString_ActionStart)) updateHints(logs, running, editable);
        bool changedLog = logs != logOpen;
        logOpen = logs;
        const char* nextTitle = logs ? state.log.title : title;
        if (changedLog) frame->setTitle(nextTitle);
        if (changedLog) {
            info->setVisibility(logs ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
            logScroll->setVisibility(logs ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (logs) {
            if (!haveLog || logGeneration != state.log_generation) {
                for (int i = 0; i < DGLAB_SCREEN_LOG_LINES; ++i) {
                    bool present = i < state.log.count;
                    std::string next = present && state.log.lines[i] ? state.log.lines[i] : "";
                    if (next != logText[i]) { logLabels[i]->setText(next); logText[i] = next; }
                    auto visibility = present ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
                    if (logLabels[i]->getVisibility() != visibility) logLabels[i]->setVisibility(visibility);
                }
                logGeneration = state.log_generation; haveLog = true;
            }
            logScroll->setContentOffsetY(state.log.offset, false);
        }
    }
};
Gameplay::Gameplay(DglabMenuResult page) : impl(std::make_unique<Impl>(page)) {}
Gameplay::~Gameplay() = default;
brls::AppletFrame* Gameplay::frame() const { return impl->frame; }
void Gameplay::update(const DglabGameplayState& state) { impl->update(state); }
}
