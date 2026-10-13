// Render the real Borealis/Yoga/NanoVG page tree with host fonts and CoreGraphics.
// This is layout QA, not a simulation of Switch OpenGL or Nintendo shared fonts.
#include "../../../nro/source/borealis/gameplay.hpp"
#include "../../../nro/source/borealis/about.hpp"
extern "C" {
#include <dglab/ui/strings.h>
#include <dglab/ui/theme.h>
#include <dglab/ui/page.h>
#include <dglab/ui/presentation.h>
}
#include <borealis/core/application.hpp>
#include <borealis/core/activity.hpp>
#include <borealis/core/font.hpp>
#include <borealis/core/ime.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/hint.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <glad/glad.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
std::string hostFont;
unsigned pixelWidth = 1280, pixelHeight = 720;
struct Texture { int w, h, channels; std::vector<unsigned char> pixels; };
struct Raster {
    CGContextRef cg = nullptr;
    std::vector<unsigned char> pixels;
    std::map<int, Texture> textures;
    int nextTexture = 0;
    ~Raster() { if (cg) CGContextRelease(cg); }
    void resize() {
        if (cg) CGContextRelease(cg);
        pixels.resize(pixelWidth * pixelHeight * 4);
        auto colors = CGColorSpaceCreateDeviceRGB();
        cg = CGBitmapContextCreate(pixels.data(), pixelWidth, pixelHeight, 8, pixelWidth * 4,
            colors, kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(colors);
        assert(cg);
        CGContextTranslateCTM(cg, 0, pixelHeight); CGContextScaleCTM(cg, 1, -1);
    }
    void color(NVGcolor c) { CGContextSetRGBFillColor(cg, c.r, c.g, c.b, c.a); }
    void clip(NVGscissor* s) {
        if (s->extent[0] < 0 || s->extent[1] < 0) return;
        float x = s->xform[4], y = s->xform[5];
        float w = std::abs(s->xform[0]) * s->extent[0] + std::abs(s->xform[2]) * s->extent[1];
        float h = std::abs(s->xform[1]) * s->extent[0] + std::abs(s->xform[3]) * s->extent[1];
        CGContextClipToRect(cg, CGRectMake(x - w, y - h, 2 * w, 2 * h));
    }
    void save(const std::filesystem::path& path) {
        auto image = CGBitmapContextCreateImage(cg);
        auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8*>(path.c_str()), path.string().size(), false);
        auto output = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
        assert(output); CGImageDestinationAddImage(output, image, nullptr);
        assert(CGImageDestinationFinalize(output));
        CFRelease(output); CFRelease(url); CGImageRelease(image);
    }
    void checkQr(brls::View* view, const char* url) {
        DglabQrCode code{};
        assert(dglabQrEncodeString(&code, url, DglabQrEcc_M));
        float scale = float(pixelHeight) / 720;
        auto box = view->getFrame(); DglabQrPixels layout{};
        assert(dglabQrPixels(box.getMinX() * scale, box.getMinY() * scale,
            box.getWidth() * scale, box.getHeight() * scale, code.size,
            int(8 * scale), int(4 * scale), &layout));
        // Check every physical pixel, including quiet zone and module edges.
        for (int y = 0; y < layout.side; ++y) for (int x = 0; x < layout.side; ++x) {
            int col = x / layout.module - 4, row = y / layout.module - 4;
            bool black = row >= 0 && col >= 0 && row < code.size && col < code.size && code.modules[row][col];
            const auto* pixel = pixels.data() + ((layout.y + y) * pixelWidth + layout.x + x) * 4;
            assert(pixel[0] == (black ? 0 : 255) && pixel[1] == pixel[0] && pixel[2] == pixel[0]);
        }
    }
};
NVGcontext* context(Raster* r) {
    NVGparams p{}; p.userPtr = r; p.edgeAntiAlias = 0;
    p.renderCreate = [](void*) { return 1; };
    p.renderDelete = [](void*) {};
    p.renderCreateTexture = [](void* user, int type, int w, int h, int, const unsigned char* data) {
        auto* r = static_cast<Raster*>(user); int id = ++r->nextTexture;
        Texture t{w, h, type == NVG_TEXTURE_ALPHA ? 1 : 4, {}};
        t.pixels.resize(w * h * t.channels);
        if (data) std::copy(data, data + t.pixels.size(), t.pixels.begin());
        r->textures.emplace(id, std::move(t)); return id;
    };
    p.renderDeleteTexture = [](void* user, int id) { static_cast<Raster*>(user)->textures.erase(id); return 1; };
    p.renderUpdateTexture = [](void* user, int id, int, int, int, int, const unsigned char* data) {
        auto& t = static_cast<Raster*>(user)->textures.at(id);
        std::copy(data, data + t.pixels.size(), t.pixels.begin()); return 1;
    };
    p.renderGetTextureSize = [](void* user, int id, int* w, int* h) {
        auto& t = static_cast<Raster*>(user)->textures.at(id); *w = t.w; *h = t.h; return 1;
    };
    p.renderViewport = [](void*, float, float, float) {};
    p.renderCancel = [](void*) {}; p.renderFlush = [](void*) {};
    p.renderFill = [](void* user, NVGpaint* paint, NVGcompositeOperationState,
        NVGscissor* scissor, float, const float*, const NVGpath* paths, int count) {
        auto* r = static_cast<Raster*>(user); auto cg = r->cg;
        CGContextSaveGState(cg); r->clip(scissor); r->color(paint->innerColor); CGContextBeginPath(cg);
        for (int i = 0; i < count; ++i) {
            auto& path = paths[i]; if (!path.nfill) continue;
            CGContextMoveToPoint(cg, path.fill[0].x, path.fill[0].y);
            for (int j = 1; j < path.nfill; ++j) CGContextAddLineToPoint(cg, path.fill[j].x, path.fill[j].y);
            CGContextClosePath(cg);
        }
        CGContextFillPath(cg); CGContextRestoreGState(cg);
    };
    p.renderStroke = [](void* user, NVGpaint* paint, NVGcompositeOperationState,
        NVGscissor* scissor, float, float, const NVGpath* paths, int count) {
        auto* r = static_cast<Raster*>(user); auto cg = r->cg;
        CGContextSaveGState(cg); r->clip(scissor); r->color(paint->innerColor);
        for (int i = 0; i < count; ++i) for (int j = 2; j < paths[i].nstroke; ++j) {
            const auto* v = paths[i].stroke + j - 2; CGContextBeginPath(cg);
            CGContextMoveToPoint(cg, v[0].x, v[0].y); CGContextAddLineToPoint(cg, v[1].x, v[1].y);
            CGContextAddLineToPoint(cg, v[2].x, v[2].y); CGContextClosePath(cg); CGContextFillPath(cg);
        }
        CGContextRestoreGState(cg);
    };
    p.renderTriangles = [](void* user, NVGpaint* paint, NVGcompositeOperationState,
        NVGscissor* scissor, const NVGvertex* vertices, int count, float) {
        auto* r = static_cast<Raster*>(user); if (!paint->image || count < 6) return;
        auto& t = r->textures.at(paint->image); if (t.channels != 1) return;
        std::vector<unsigned char> rgba(t.w * t.h * 4);
        for (int i = 0; i < t.w * t.h; ++i) {
            float a = t.pixels[i] * paint->innerColor.a;
            rgba[i * 4] = a * paint->innerColor.r; rgba[i * 4 + 1] = a * paint->innerColor.g;
            rgba[i * 4 + 2] = a * paint->innerColor.b; rgba[i * 4 + 3] = a;
        }
        auto colors = CGColorSpaceCreateDeviceRGB();
        auto provider = CGDataProviderCreateWithData(nullptr, rgba.data(), rgba.size(), nullptr);
        auto image = CGImageCreate(t.w, t.h, 8, 32, t.w * 4, colors, kCGImageAlphaPremultipliedLast,
            provider, nullptr, false, kCGRenderingIntentDefault);
        auto cg = r->cg; CGContextSaveGState(cg); r->clip(scissor);
        for (int i = 0; i + 5 < count; i += 6) {
            float x0 = vertices[i].x, x1 = x0, y0 = vertices[i].y, y1 = y0;
            float u0 = vertices[i].u, u1 = u0, v0 = vertices[i].v, v1 = v0;
            for (int j = 1; j < 6; ++j) {
                auto& v = vertices[i + j]; x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
                y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
                u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v);
            }
            auto glyph = CGImageCreateWithImageInRect(image, CGRectMake(std::round(u0 * t.w), std::round(v0 * t.h),
                std::round((u1 - u0) * t.w), std::round((v1 - v0) * t.h)));
            if (!glyph) continue;
            CGContextSaveGState(cg); CGContextTranslateCTM(cg, x0, y1); CGContextScaleCTM(cg, 1, -1);
            CGContextDrawImage(cg, CGRectMake(0, 0, x1 - x0, y1 - y0), glyph);
            CGContextRestoreGState(cg); CGImageRelease(glyph);
        }
        CGContextRestoreGState(cg); CGImageRelease(image); CGDataProviderRelease(provider); CGColorSpaceRelease(colors);
    };
    return nvgCreateInternal(&p);
}
class Video : public VideoContext {
public:
    Raster raster;
    NVGcontext* vg;
    Video() { raster.resize(); vg = context(&raster); }
    ~Video() override { nvgDeleteInternal(vg); }
    void clear(NVGcolor c) override { raster.color(c); CGContextFillRect(raster.cg, CGRectMake(0, 0, pixelWidth, pixelHeight)); }
    void beginFrame() override {}
    void endFrame() override {}
    void resetState() override {}
    double getScaleFactor() override { return 1; }
    NVGcontext* getNVGContext() override { return vg; }
    void setSwapInterval(int) override {}
};
class Input : public brls::InputManager {
public:
    short getControllersConnectedCount() override { return 1; }
    void updateUnifiedControllerState(brls::ControllerState* s) override { *s = {}; }
    void updateControllerState(brls::ControllerState* s, int) override { *s = {}; }
    bool getKeyboardKeyState(brls::BrlsKeyboardScancode) override { return false; }
    void updateTouchStates(std::vector<brls::RawTouchState>* s) override { s->clear(); }
    void updateMouseStates(brls::RawMouseState* s) override { *s = {}; }
    void sendRumble(unsigned short, unsigned short, unsigned short) override {}
};
class Ime : public brls::ImeManager {
public:
    bool openForText(std::function<void(std::string)>, std::string, std::string, int, std::string, int) override { return false; }
    bool openForPassword(std::function<void(std::string)>, std::string, std::string, int, std::string) override { return false; }
    bool openForNumber(std::function<void(long)>, std::string, std::string, int, std::string, std::string, std::string, int) override { return false; }
};
class Fonts : public brls::FontLoader {
public:
    void loadFonts() override {
        assert(brls::Application::loadFontFromFile(brls::FONT_REGULAR, hostFont));
        assert(brls::Application::loadFontFromFile(brls::FONT_SWITCH_ICONS, hostFont));
        assert(loadMaterialFromResources());
    }
};
class Platform : public brls::Platform {
    std::unique_ptr<Video> video;
    Input input; Ime ime; Fonts fonts; brls::NullAudioPlayer audio;
    brls::ThemeVariant variant = brls::ThemeVariant::DARK;
public:
    void createWindow(std::string, uint32_t, uint32_t, float, float) override {
        video = std::make_unique<Video>(); brls::Application::setWindowSize(pixelWidth, pixelHeight);
    }
    std::string getName() override { return "headless layout QA"; }
    int getWirelessLevel() override { return 0; } std::string getIpAddress() override { return ""; }
    std::string getDnsServer() override { return ""; } int getBatteryLevel() override { return 0; }
    bool isBatteryCharging() override { return false; }
    bool canShowBatteryLevel() override { return false; } bool canShowWirelessLevel() override { return false; }
    void disableScreenDimming(bool, const std::string&, const std::string&) override {}
    bool isScreenDimmingDisabled() override { return false; } void setBacklightBrightness(float) override {}
    float getBacklightBrightness() override { return 1; } bool canSetBacklightBrightness() override { return false; }
    bool mainLoopIteration() override { return true; }
    brls::ThemeVariant getThemeVariant() override { return variant; }
    void setThemeVariant(brls::ThemeVariant v) override { variant = v; }
    std::string getLocale() override { return "en-US"; }
    brls::AudioPlayer* getAudioPlayer() override { return &audio; } VideoContext* getVideoContext() override { return video.get(); }
    brls::InputManager* getInputManager() override { return &input; } brls::ImeManager* getImeManager() override { return &ime; }
    brls::FontLoader* getFontLoader() override { return &fonts; } bool isApplicationMode() override { return true; }
    void exitToHomeMode(bool) override {} void forceEnableGamePlayRecording() override {} void openBrowser(std::string) override {}
};
void viewport(GLenum, GLint* v) { v[0] = v[1] = 0; v[2] = pixelWidth; v[3] = pixelHeight; }
void checkLayout(brls::View* view, bool log = false) {
    if (view->getVisibility() == brls::Visibility::GONE) return;
    if (dynamic_cast<brls::ScrollingFrame*>(view)) log = true;
    auto r = view->getFrame();
    if (!log && view->getVisibility() == brls::Visibility::VISIBLE) {
        if (r.getMinX() < -1 || r.getMinY() < -1 || r.getMaxX() > 1281 || r.getMaxY() > 721)
            throw std::runtime_error("layout outside 720p bounds: " + view->describe());
    }
    if (auto* box = dynamic_cast<brls::Box*>(view)) for (auto* child : box->getChildren()) checkLayout(child, log);
}
unsigned checkHintFonts(brls::View* view) {
    unsigned count = 0;
    if (auto* hint = dynamic_cast<brls::Hint*>(view)) {
        auto* icon = dynamic_cast<brls::Label*>(hint->getView("icon"));
        assert(icon && icon->getFont() == brls::Application::getFont(brls::FONT_SWITCH_ICONS));
        count = 1;
    }
    if (auto* box = dynamic_cast<brls::Box*>(view))
        for (auto* child : box->getChildren()) count += checkHintFonts(child);
    return count;
}
}
namespace brls { Platform* Platform::createPlatform() { return new ::Platform(); } }
int main(int argc, char** argv) {
    if ((argc != 4 && argc != 5) || (argc == 5 && std::string(argv[4]) != "--quick")) {
        std::fprintf(stderr, "usage: preview <repo> <host-CJK-font> <output-directory> [--quick]\n"); return 2;
    }
    hostFont = argv[2]; std::filesystem::path output = argv[3]; std::filesystem::create_directories(output);
    for (auto lang : {DglabLanguage_English, DglabLanguage_ChineseSimplified}) {
        std::ifstream file(std::filesystem::path(argv[1]) / "lang" / (lang == DglabLanguage_English ? "en.json" : "zh-Hans.json"));
        std::string content((std::istreambuf_iterator<char>(file)), {}); DglabStringsReport report;
        assert(dglabStringsLoadJson(lang, content.c_str(), content.size(), &report) == DglabStringsLoad_Ok);
    }
    glad_glGetIntegerv = viewport;
    brls::Application::init(); brls::Application::setGlobalQuit(false); brls::Application::setAutomaticDeactivation(false);
    unsigned captures = 0;
    for (unsigned resolution : {720u, 1080u}) {
        if (argc == 5 && resolution != 720) continue;
        pixelHeight = resolution; pixelWidth = resolution * 16 / 9;
        for (bool dark : {false, true}) {
            if (argc == 5 && dark) continue;
            dglabThemeSet(dark ? &dglabThemeDark : &dglabThemeLight);
            brls::Application::getPlatform()->setThemeVariant(dark ? brls::ThemeVariant::DARK : brls::ThemeVariant::LIGHT);
            brls::Application::createWindow("layout QA");
            for (auto lang : {DglabLanguage_English, DglabLanguage_ChineseSimplified}) {
                if (argc == 5 && lang != DglabLanguage_English) continue;
                dglabStringsSetLanguage(lang);
                for (int scenario = 0; scenario < 12; ++scenario) {
                    if (scenario == 8) {
                        DglabAboutState state{};
                        state.app_version = "0.0.0-test"; state.build_id = "v0.0.0-109-g0123456-dirty";
                        state.github_url = "https://github.com/example/DGLAB-NX";
                        state.ipc_version.major = 1; state.preference = DglabLanguage_Auto;
                        state.resolved = lang; state.theme = DglabThemeMode_Auto; state.theme_system_is_dark = dark;
                        auto* content = new brls::Box(brls::Axis::COLUMN);
                        content->setPadding(16, 54, 16, 54); content->setFocusable(true); content->setHideHighlight(true);
                        dglab::buildAboutContent(content, state);
                        auto* list = dynamic_cast<brls::Box*>(content->getView("dglab/about/list"));
                        assert(content->getChildren().size() == 3 && list && list->getChildren().size() == 6);
                        for (auto* row : list->getChildren()) assert(dynamic_cast<brls::DetailCell*>(row));
                        auto* scroll = new brls::ScrollingFrame(); scroll->setContentView(content);
                        auto* frame = new brls::AppletFrame(scroll); frame->setTitle(dglabString(DglabString_AboutTitle));
                        frame->registerAction(dglabString(DglabString_ActionLanguage), brls::BUTTON_LEFT,
                            [](brls::View*) { return true; });
                        frame->registerAction(dglabString(DglabString_ActionLanguage), brls::BUTTON_RIGHT,
                            [](brls::View*) { return true; });
                        brls::Application::pushActivity(new brls::Activity(frame), brls::TransitionAnimation::NONE);
                        for (int i = 0; i < 3; ++i) assert(brls::Application::mainLoop());
                        // Back, default confirm, and one combined left/right language hint.
                        assert(checkHintFonts(frame->getFooter()) == 3);
                        assert(frame->getFooter()->getView("brls/hints/time")->getVisibility() == brls::Visibility::GONE);
                        checkLayout(frame);
                        auto* video = static_cast<Video*>(brls::Application::getPlatform()->getVideoContext());
                        video->raster.save(output / (std::to_string(resolution) + (dark ? "-dark-" : "-light-") +
                            (lang == DglabLanguage_English ? "en-" : "zh-") + "about.png"));
                        ++captures; brls::Application::releaseWindowState(false);
                        continue;
                    }
                    DglabGameplayState s{};
                    s.page = scenario == 0 || scenario == 4 || scenario == 6 ? DglabMenuResult_Socket :
                        scenario == 1 || scenario == 5 || scenario >= 9 ? DglabMenuResult_Ble :
                        scenario == 2 ? DglabMenuResult_Motion : DglabMenuResult_Touch;
                    std::array<std::string, DGLAB_SCREEN_LOG_LINES> lines;
                    std::array<const char*, DGLAB_SCREEN_LOG_LINES> pointers;
                    for (size_t i = 0; i < lines.size(); ++i) {
                        lines[i] = "log " + std::to_string(i) + ": " + std::string(180, 'W'); pointers[i] = lines[i].c_str();
                    }
                    s.log = {dglabString(s.page == DglabMenuResult_Ble ? DglabString_BleLogTitle : DglabString_LogTitle), pointers.data(), 32, 665, true};
                    s.log_generation = 1;
                    if (s.page == DglabMenuResult_Socket) {
                        auto& v = s.value.socket; v.sysmodule_ok = v.status_ok = v.url_ok = true;
                        v.url = "https://www.dungeon-lab.com/app-download.php#DGLAB-SOCKET#ws://192.168.100.123:9999/aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
                        v.status.state = scenario == 6 ? DglabNetState_Stopped : DglabNetState_Paired;
                        std::snprintf(reinterpret_cast<char*>(v.status.ip_text), sizeof(v.status.ip_text), "192.168.100.123"); v.status.port = 9999;
                        std::snprintf(reinterpret_cast<char*>(v.status.peer_id), sizeof(v.status.peer_id), "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
                        v.limit_a = v.limit_b = 100; v.test_strength_a = 80; v.test_strength_b = 12;
                        v.last_command = "A Test  rejected 0x12345678"; v.last_command_tone = DglabCmdTone_Error;
                        v.log_open = scenario == 4;
                    } else if (s.page == DglabMenuResult_Ble) {
                        auto& v = s.value.ble; v.sysmodule_ok = true; v.address_valid = scenario != 9;
                        v.status.state = scenario >= 9 ? DglabBleState_Idle : DglabBleState_Connected;
                        if (scenario == 10) v.edit_result = DglabBleEdit_Invalid;
                        if (scenario == 11) v.edit_result = DglabBleEdit_SaveFailed;
                        v.limit_a = v.limit_b = 100; v.strength_a = 80; v.strength_b = 12; v.log_open = scenario == 5;
                    } else if (s.page == DglabMenuResult_Motion) {
                        auto& v = s.value.motion; v.sysmodule_ok = v.left_connected = v.right_connected = v.server_running = true;
                        v.moving_a = v.moving_b = true; v.level_a = 99; v.frequency_a = v.frequency_b = 100;
                        v.limit_a = v.limit_b = 100; v.channel_strength_a = 80; v.channel_strength_b = 12;
                        v.link = "BLE"; v.last_upload = "waveform B  rejected 0x12345678"; v.last_upload_tone = DglabCmdTone_Warn;
                    } else {
                        auto& v = s.value.touch; v.sysmodule_ok = true; v.limit_a = v.limit_b = 100;
                        v.docked = scenario == 7; v.held_a = v.held_b = true; v.x_a = 140; v.y_a = 560; v.x_b = 1100; v.y_b = 530;
                        v.level_a = v.level_b = 80; v.frequency_a = v.frequency_b = 100;
                        v.link = "BLE"; v.last_upload = "waveform A ok";
                    }
                    dglab::Gameplay page(s.page);
                    brls::Application::pushActivity(new brls::Activity(page.frame()), brls::TransitionAnimation::NONE);
                    for (int frame = 0; frame < 3; ++frame) { page.update(s); assert(brls::Application::mainLoop()); }
                    checkLayout(page.frame());
                    assert(checkHintFonts(page.frame()->getFooter()) > 0);
                    assert(page.frame()->getFooter()->getView("brls/hints/time")->getVisibility() == brls::Visibility::GONE);
                    auto* video = static_cast<Video*>(brls::Application::getPlatform()->getVideoContext());
                    if (scenario == 0) video->raster.checkQr(page.frame()->getView("dglab/qr"), s.value.socket.url);
                    if (scenario == 4 || scenario == 5) {
                        assert(page.frame()->getActions().size() == 2);
                        for (const auto& action : page.frame()->getActions())
                            assert(action->getButton() == brls::BUTTON_Y || action->getButton() == brls::BUTTON_B);
                    }
                    video->raster.save(output / (std::to_string(resolution) + (dark ? "-dark-" : "-light-") +
                        (lang == DglabLanguage_English ? "en-" : "zh-") + std::to_string(scenario) + ".png"));
                    ++captures; brls::Application::releaseWindowState(false);
                }
            }
            brls::Application::releaseWindowState();
        }
    }
    brls::Application::shutdown();
    std::printf("%u actual Borealis page layout captures passed (host fonts; Switch graphics/input require hardware).\n", captures);
}
