extern "C" {
#include <dglab/ui/native.h>
#include <dglab/ui/advanced.h>
#include <dglab/ui/input_state.h>
#include <dglab/ui/strings.h>
}
#include "gameplay.hpp"
#include "about.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/activity.hpp>
#include <borealis/core/font.hpp>
#include <borealis/core/ime.hpp>
#include <borealis/platforms/glfw/glfw_video.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/label.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
DglabNativeHost host{};
std::string error;
bool initialized = false, active = false, romfsReady = false;
bool navigationEnabled = true;
bool gameplayMode = false;
std::function<void()> beforeFrame;
DglabUiKeys keys{};

std::array<u64, 240> frameTimes{};
size_t frameCount = 0;
unsigned metricPage = 0;
void metrics(const char* event, u64 elapsed = 0) {
    if (!host.log) return;
    u64 used = 0;
    Result rc = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    char line[192];
    std::snprintf(line, sizeof(line), "ui %s page=%u ns=%llu used_bytes=%llu memory_rc=0x%08X applet=%u",
        event, metricPage, (unsigned long long)elapsed, (unsigned long long)used, rc, (unsigned)appletGetAppletType());
    host.log(line);
}
void frameMetrics() {
    if (!frameCount || !host.log) return;
    auto samples = frameTimes;
    std::sort(samples.begin(), samples.begin() + frameCount);
    char line[160];
    std::snprintf(line, sizeof(line), "ui frames page=%u samples=%u p95_ns=%llu max_ns=%llu",
        metricPage, (unsigned)frameCount, (unsigned long long)samples[(frameCount - 1) * 95 / 100],
        (unsigned long long)samples[frameCount - 1]);
    host.log(line); frameCount = 0;
}

class Input : public brls::InputManager {
public:
    void runloopStart() override {} // Sampling also runs while framework input is blocked.
    short getControllersConnectedCount() override { return 1; }
    void updateUnifiedControllerState(brls::ControllerState* state) override {
        // Repetition deadlines belong to Borealis; do not overwrite them.
        std::fill(std::begin(state->buttons), std::end(state->buttons), false);
        std::fill(std::begin(state->axes), std::end(state->axes), 0.0f);
        if (gameplayMode) return; // Business shortcuts are handled once by the C controller.
        const std::pair<brls::ControllerButton, u64> map[] = {
            {brls::BUTTON_A, HidNpadButton_A}, {brls::BUTTON_B, HidNpadButton_B},
            {brls::BUTTON_Y, HidNpadButton_Y}, {brls::BUTTON_LB, HidNpadButton_L},
            {brls::BUTTON_RB, HidNpadButton_R},
            {brls::BUTTON_NAV_UP, HidNpadButton_Up},
            {brls::BUTTON_NAV_DOWN, HidNpadButton_Down},
        };
        for (const auto& entry : map) {
            bool navigation = entry.first == brls::BUTTON_NAV_UP || entry.first == brls::BUTTON_NAV_DOWN;
            state->buttons[entry.first] = (!navigation || navigationEnabled) && (keys.held & entry.second) != 0;
        }
    }
    void updateControllerState(brls::ControllerState* state, int) override { updateUnifiedControllerState(state); }
    bool getKeyboardKeyState(brls::BrlsKeyboardScancode) override { return false; }
    void updateTouchStates(std::vector<brls::RawTouchState>* states) override { states->clear(); }
    void updateMouseStates(brls::RawMouseState* state) override { *state = {}; }
    void sendRumble(unsigned short, unsigned short, unsigned short) override {}
};

class Ime : public brls::ImeManager {
public:
    bool openForText(std::function<void(std::string)>, std::string, std::string, int, std::string, int) override { return false; }
    bool openForPassword(std::function<void(std::string)>, std::string, std::string, int, std::string) override { return false; }
    bool openForNumber(std::function<void(long)>, std::string, std::string, int, std::string, std::string, std::string, int) override { return false; }
};

class Fonts : public brls::FontLoader {
    void shared(const std::string& name, PlSharedFontType type) {
        PlFontData font{};
        Result rc = plGetSharedFontByType(&font, type);
        if (R_FAILED(rc) || !brls::Application::loadFontFromMemory(name, font.address, font.size, false))
            throw std::runtime_error("shared font unavailable: " + name);
    }
public:
    void loadFonts() override {
        shared(brls::FONT_REGULAR, PlSharedFontType_Standard);
        shared(brls::FONT_CHINESE_SIMPLIFIED, PlSharedFontType_ChineseSimplified);
        shared(brls::FONT_SWITCH_ICONS, PlSharedFontType_NintendoExt);
        auto* vg = brls::Application::getNVGContext();
        nvgAddFallbackFontId(vg, brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED),
            brls::Application::getFont(brls::FONT_REGULAR));
        nvgAddFallbackFontId(vg, brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED),
            brls::Application::getFont(brls::FONT_SWITCH_ICONS));
        if (!loadMaterialFromResources()) throw std::runtime_error("RomFS MaterialIcons font unavailable");
    }
};

class Platform : public brls::Platform {
    std::unique_ptr<brls::GLFWVideoContext> video;
    Input input;
    Ime ime;
    Fonts fonts;
    brls::NullAudioPlayer audio;
    brls::ThemeVariant theme = brls::ThemeVariant::DARK;
    bool plReady = false;
public:
    ~Platform() override { suspend(); }
    void suspend() {
        video.reset(); // NanoVG no longer references shared font memory.
        if (plReady) { plExit(); plReady = false; }
    }
    void createWindow(std::string title, uint32_t width, uint32_t height, float x, float y) override {
        Result rc = plInitialize(PlServiceType_User);
        if (R_FAILED(rc)) throw std::runtime_error("pl:u initialization failed");
        plReady = true;
        try {
            video = std::make_unique<brls::GLFWVideoContext>(title, width, height, x, y);
            if (!video->getNVGContext()) throw std::runtime_error("NanoVG initialization failed");
        } catch (...) { host.exit_cleanup(host.service); suspend(); throw; }
    }
    std::string getName() override { return "DGLAB-NX Switch OpenGL"; }
    bool mainLoopIteration() override {
        if (!appletMainLoop()) {
            host.exit_cleanup(host.service);
            beforeFrame = {};
            return false;
        }
        padUpdate(host.pad);
        dglabUiKeysUpdate(&keys, padGetButtons(host.pad));
        if (beforeFrame) beforeFrame();
        return true;
    }
    brls::ThemeVariant getThemeVariant() override { return theme; }
    void setThemeVariant(brls::ThemeVariant value) override { theme = value; }
    std::string getLocale() override { return "en-US"; } // Business text uses dglabString.
    VideoContext* getVideoContext() override { return video.get(); }
    brls::AudioPlayer* getAudioPlayer() override { return &audio; }
    brls::InputManager* getInputManager() override { return &input; }
    brls::FontLoader* getFontLoader() override { return &fonts; }
    brls::ImeManager* getImeManager() override { return &ime; }
    bool canShowBatteryLevel() override { return false; }
    bool canShowWirelessLevel() override { return false; }
    int getBatteryLevel() override { return 0; }
    bool isBatteryCharging() override { return false; }
    int getWirelessLevel() override { return 0; }
    std::string getIpAddress() override { return ""; }
    std::string getDnsServer() override { return ""; }
    // Sleep policy and service ownership remain with the C host.
    void disableScreenDimming(bool, const std::string&, const std::string&) override {}
    bool isScreenDimmingDisabled() override { return false; }
    void setBacklightBrightness(float) override {}
    float getBacklightBrightness() override { return 1.0f; }
    bool canSetBacklightBrightness() override { return false; }
    bool isApplicationMode() override { return appletGetAppletType() == AppletType_Application; }
    void exitToHomeMode(bool) override {}
    void forceEnableGamePlayRecording() override {}
    void openBrowser(std::string) override {}
};
Platform* platform = nullptr;

void resetKeys() {
    padUpdate(host.pad);
    dglabUiKeysReset(&keys, padGetButtons(host.pad));
}
void applyTheme() {
    platform->setThemeVariant(host.is_dark() ? brls::ThemeVariant::DARK : brls::ThemeVariant::LIGHT);
}

brls::Label* label(const std::string& text, float size = DGLAB_TEXT_BODY) {
    auto* view = new brls::Label();
    view->setText(text);
    view->setFontSize(size);
    view->setSingleLine(false);
    view->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    view->setMarginBottom(12);
    return view;
}
class Row : public brls::DetailCell {
public:
    std::function<void()> focused;
    void onFocusGained() override {
        brls::DetailCell::onFocusGained();
        if (focused) focused();
    }
};

enum class Page {
    Menu = 0, Socket = DglabMenuResult_Socket, Ble = DglabMenuResult_Ble,
    Motion = DglabMenuResult_Motion, Touch = DglabMenuResult_Touch,
    Parameters = DglabMenuResult_Advanced, About = DglabMenuResult_About,
};
bool isGameplay(Page page) {
    return page >= Page::Socket && page <= Page::Touch;
}
struct Screen {
    Page page;
    std::unique_ptr<dglab::Gameplay> gameplay;
    brls::AppletFrame* frame = nullptr;
    brls::ScrollingFrame* scroll = nullptr;
    brls::Box* content = nullptr;
    brls::Box* body = nullptr;
    brls::Label* status = nullptr;
    brls::Label* note = nullptr;
    brls::Label* saveNote = nullptr;
    brls::Label* bleNote = nullptr;
    std::vector<Row*> rows;
    DglabMotionFeedConfig config{};
    DglabUiRepeat repeat{};
    unsigned selected = 0;
    bool saved = true;
    bool live = false, ble = false;
    bool haveStatus = false;
    bool finish = false;
    bool rebuild = false;
    Page next = Page::Menu;
    DglabMenuResult result = DglabMenuResult_Exit;
    DglabAboutState about{};
    float offset = 0;
    bool restoreOffset = false;

    explicit Screen(Page kind) : page(kind) {
        if (isGameplay(page)) {
            gameplay = std::make_unique<dglab::Gameplay>(static_cast<DglabMenuResult>(page));
            frame = gameplay->frame();
            return;
        }
        content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(16, 54, 16, 54);
        scroll = new brls::ScrollingFrame();
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        scroll->setContentView(content);
        body = new brls::Box(brls::Axis::COLUMN);
        scroll->setGrow(1);
        body->addView(scroll);
        frame = new brls::AppletFrame(body);
        // Replace default back action so no framework dismiss/global quit leaks in.
        std::vector<brls::ActionIdentifier> old;
        for (auto action : frame->getActions()) old.push_back(action->getIdentifier());
        for (auto id : old) frame->unregisterAction(id);
        frame->registerAction(dglabString(page == Page::Menu ? DglabString_ActionExit : DglabString_ActionBack),
            brls::BUTTON_B, [this](brls::View*) {
                if (page == Page::Parameters && !host.save_config(&config)) {
                    saved = false; updateValues(); return true;
                }
                finish = true; next = Page::Menu; return true;
            });
        status = label("", DGLAB_TEXT_VALUE);
        status->setSingleLine(true);
        frame->getHeader()->addView(status);
        if (page == Page::Menu) buildMenu();
        else if (page == Page::About) buildAbout();
        else buildParameters();
    }
    void addRow(unsigned index, const std::string& name, const std::string& value) {
        auto* row = new Row();
        row->setText(name); row->setDetailText(value);
        row->focused = [this, index] {
            selected = index;
            dglabUiRepeatReset(&repeat);
            if (page == Page::Menu) {
                *host.menu_selected = selected;
                note->setText(dglabMenuItemDescription(selected));
            } else if (page == Page::Parameters) note->setText(dglabAdvancedDescription(selected));
        };
        row->registerAction(dglabString(DglabString_ActionEnter), brls::BUTTON_A,
            [this, index](brls::View*) {
                if (page != Page::Menu) return true;
                selected = index; *host.menu_selected = index;
                if (index == DglabMenu_ItemBle) {
                    auto check = host.ble_check();
                    if (check != DglabBlePatch_Ok) {
                        note->setText(dglabBlePatchMessage(check));
                        return true;
                    }
                }
                if (index == DglabMenu_ItemAbout) { finish = true; next = Page::About; }
                else if (index == DglabMenu_ItemAdvanced) { finish = true; next = Page::Parameters; }
                else if (index <= DglabMenu_ItemTouch) {
                    finish = true; next = static_cast<Page>(index + 1);
                }
                return true;
            }, page != Page::Menu);
        content->addView(row); rows.push_back(row);
    }
    void clampNavigation() {
        if (rows.empty()) return;
        rows.front()->setCustomNavigationRoute(brls::FocusDirection::UP, rows.front());
        rows.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, rows.back());
    }
    void buildMenu() {
        frame->setTitle(dglabString(DglabString_MenuTitle));
        note = label("");
        for (unsigned i = 0; i < DglabMenu_ItemCount; ++i) addRow(i, dglabMenuItemName(i), "");
        body->addView(note);
        note->setMargins(8, 54, 8, 54);
        bleNote = label("", DGLAB_TEXT_NOTE); body->addView(bleNote);
        bleNote->setMargins(0, 54, 8, 54);
        selected = std::min(*host.menu_selected, static_cast<unsigned>(DglabMenu_ItemCount - 1));
        note->setText(dglabMenuItemDescription(selected));
        clampNavigation();
    }
    void buildAbout() {
        frame->setTitle(dglabString(DglabString_AboutTitle));
        host.about_state(&about);
        content->setFocusable(true);
        content->setHideHighlight(true);
        scroll->setHideHighlight(true);
        dglab::buildAboutContent(content, about);
        frame->registerAction(dglabString(DglabString_ActionLanguage), brls::BUTTON_LEFT,
            [](brls::View*) { return true; });
        frame->registerAction(dglabString(DglabString_ActionLanguage), brls::BUTTON_RIGHT,
            [](brls::View*) { return true; });
        frame->registerAction(dglabString(DglabString_ActionTheme), brls::BUTTON_Y,
            [this](brls::View*) { host.cycle_theme(); applyTheme(); rebuild = true; return true; });
    }
    void buildParameters() {
        host.load_config(&config);
        frame->setTitle(dglabString(DglabString_AdvancedTitle));
        note = label("");
        for (unsigned i = 0; i < DglabMotionSetting_Count; ++i) addRow(i, dglabAdvancedName(i), "");
        body->addView(note);
        note->setMargins(8, 54, 8, 54);
        saveNote = label("", DGLAB_TEXT_NOTE); body->addView(saveNote);
        saveNote->setMargins(0, 54, 8, 54);
        frame->registerAction(dglabString(DglabString_ActionReset), brls::BUTTON_Y,
            [this](brls::View*) {
                dglabMotionSettingsDefault(&config); saved = host.save_config(&config);
                dglabUiRepeatReset(&repeat); updateValues(); return true;
            });
        // Hints only: exact press and repeat timing is driven by the C helper.
        frame->registerAction(dglabString(DglabString_ActionAdjust), brls::BUTTON_LEFT,
            [](brls::View*) { return true; });
        frame->registerAction(dglabString(DglabString_ActionAdjust), brls::BUTTON_RIGHT,
            [](brls::View*) { return true; });
        updateValues(); clampNavigation();
    }
    void updateValues() {
        for (unsigned i = 0; i < rows.size(); ++i) {
            char value[48]; dglabAdvancedValue(&config, i, value, sizeof(value));
            rows[i]->setDetailText(value);
        }
        note->setText(dglabAdvancedDescription(selected));
        saveNote->setText(dglabString(saved ? DglabString_AdvancedSaved : DglabString_AdvancedSaveFailed));
    }
    void before() {
        if (gameplay) {
            if (!host.page_update(keys.down, keys.held, armTicksToNs(armGetSystemTick()))) {
                finish = true; next = Page::Menu;
                return;
            }
            gameplay->update(*host.page_state());
        } else {
            host.poll();
        }
    }
    void tick() {
        if (page == Page::Parameters) {
            int press = !!(keys.down & HidNpadButton_Right) - !!(keys.down & HidNpadButton_Left);
            int held = !!(keys.held & HidNpadButton_Right) - !!(keys.held & HidNpadButton_Left);
            int step = dglabUiRepeatStep(&repeat, press, held, armTicksToNs(armGetSystemTick()));
            if (step) { dglabMotionSettingsStep(&config, selected, step); saved = host.save_config(&config); updateValues(); }
        } else if (page == Page::About) {
            if (restoreOffset) { scroll->setContentOffsetY(offset, false); restoreOffset = false; }
            if (keys.down & (HidNpadButton_Left | HidNpadButton_Right)) { host.cycle_language(); rebuild = true; }
            int step = !!(keys.down & HidNpadButton_Down) - !!(keys.down & HidNpadButton_Up);
            if (step) scroll->setContentOffsetY(scroll->getContentOffsetY() + step * DGLAB_ABOUT_SCROLL_STEP, false);
        }
        bool nowLive = host.sysmodule_ok(host.service);
        if (!haveStatus || live != nowLive) {
            live = nowLive;
            status->setText(dglabString(live ? DglabString_SysmoduleOk : DglabString_SysmoduleDown));
        }
        if (page == Page::Menu) {
            bool nowBle = host.ble_active(host.service);
            if (!haveStatus || ble != nowBle) {
                ble = nowBle;
                bleNote->setText(ble ? dglabString(DglabString_MenuBleRunning) : "");
            }
        }
        haveStatus = true;
    }
};
void report(const char* text) {
    error = text;
    if (host.log) host.log(error.c_str());
}
}

namespace brls {
Platform* Platform::createPlatform() {
    platform = new ::Platform();
    return platform;
}
}

extern "C" bool dglabNativeInit(const DglabNativeHost* value) {
    host = *value;
    u64 started = armGetSystemTick();
    try {
        Result rc = romfsInit();
        if (R_FAILED(rc)) throw std::runtime_error("RomFS initialization failed");
        romfsReady = true;
        initialized = brls::Application::init();
        brls::Application::setGlobalQuit(false);
        brls::Application::setAutomaticDeactivation(false);
        if (!initialized) throw std::runtime_error("Borealis initialization failed");
        metrics("init", armTicksToNs(armGetSystemTick() - started));
        return true;
    } catch (const std::exception& e) {
        host.exit_cleanup(host.service); report(e.what()); return false;
    }
}
extern "C" bool dglabNativeResume() {
    if (!initialized) return false;
    if (active) return true;
    u64 started = armGetSystemTick();
    try {
        applyTheme();
        brls::Application::createWindow("DGLAB-NX");
        active = true;
        resetKeys();
        metrics("resume", armTicksToNs(armGetSystemTick() - started));
        return true;
    } catch (const std::exception& e) {
        host.exit_cleanup(host.service);
        report(e.what());
        brls::Application::releaseWindowState(); platform->suspend(); return false;
    }
}
extern "C" void dglabNativeSuspend() {
    beforeFrame = {};
    host.page_leave();
    if (!initialized) return;
    brls::Application::releaseWindowState();
    frameMetrics();
    platform->suspend(); active = false;
    metrics("suspend");
}
extern "C" void dglabNativeShutdown() {
    beforeFrame = {};
    host.exit_cleanup(host.service);
    metrics("shutdown begin");
    if (initialized) {
        dglabNativeSuspend();
        brls::Application::shutdown();
        initialized = false; platform = nullptr;
    }
    if (romfsReady) { romfsExit(); romfsReady = false; }
    metrics("shutdown end");
}
extern "C" const char* dglabNativeError() { return error.c_str(); }
extern "C" DglabMenuResult dglabNativeRun() {
    if (!active) return DglabMenuResult_Exit;
    Page page = Page::Menu;
    std::string menuError;
    float aboutOffset = 0;
    try {
        for (;;) {
            metricPage = static_cast<unsigned>(page);
            u64 pageStarted = armGetSystemTick();
            auto screen = std::make_unique<Screen>(page);
            if (page == Page::About) { screen->offset = aboutOffset; screen->restoreOffset = true; }
            resetKeys();
            navigationEnabled = page == Page::Menu || page == Page::Parameters;
            gameplayMode = isGameplay(page);
            if (gameplayMode) {
                host.page_enter(host.service, host.pad, static_cast<DglabMenuResult>(page));
                if (host.page_state()->page == DglabMenuResult_Exit) {
                    const char* message = dglabBlePatchMessage(host.ble_check());
                    menuError = message ? message : "";
                    delete screen->frame;
                    page = Page::Menu;
                    continue;
                }
            }
            beforeFrame = [current = screen.get()] { current->before(); };
            const unsigned initialSelection = screen->selected;
            brls::Application::pushActivity(new brls::Activity(screen->frame));
            metrics("page present", armTicksToNs(armGetSystemTick() - pageStarted));
            if (!screen->rows.empty()) brls::Application::giveFocus(screen->rows[initialSelection]);
            if (page == Page::Menu && !menuError.empty()) {
                screen->note->setText(menuError);
                menuError.clear();
            }
            bool running = true;
            while (!screen->finish && !screen->rebuild) {
                u64 frameStarted = armGetSystemTick();
                if (!brls::Application::mainLoop()) { running = false; break; }
                if (!screen->gameplay) screen->tick();
                frameTimes[frameCount++] = armTicksToNs(armGetSystemTick() - frameStarted);
                if (frameCount == frameTimes.size()) frameMetrics();
            }
            beforeFrame = {};
            frameMetrics();
            host.page_leave();
            if (!running) {
                // mainLoop already destroys Application; the C host owns final cleanup.
                initialized = false; active = false; platform = nullptr;
                if (romfsReady) { romfsExit(); romfsReady = false; }
                return DglabMenuResult_Exit;
            }
            if (screen->finish && page == Page::Menu && screen->next == Page::Menu) {
                if (screen->result == DglabMenuResult_Exit) host.exit_cleanup(host.service);
                brls::Application::releaseWindowState(false);
                return screen->result;
            }
            if (page == Page::About) aboutOffset = screen->scroll->getContentOffsetY();
            page = screen->rebuild ? page : screen->next;
            brls::Application::releaseWindowState(false);
        }
    } catch (const std::exception& e) {
        beforeFrame = {}; host.exit_cleanup(host.service); report(e.what()); return DglabMenuResult_Exit;
    }
}
