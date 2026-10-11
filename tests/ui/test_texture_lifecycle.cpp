// Exercise the vendored cache against a fake graphics platform. No Switch or GL
// is needed to reproduce the exit callback after the window has been destroyed.
#include <cassert>
#include <cstdio>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <borealis/core/event.hpp>

struct NVGcontext { std::vector<int> deleted; };
static void nvgDeleteImage(NVGcontext* vg, int image) {
    assert(vg); vg->deleted.push_back(image);
}
namespace brls {
struct Video {
    NVGcontext context;
    NVGcontext* getNVGContext() { return &context; }
};
struct Platform {
    Video* video = nullptr;
    Video* getVideoContext() { return video; }
};
struct Application {
    inline static Platform* platform = nullptr;
    inline static Event<> exitEvent, resizeEvent;
    inline static unsigned lookups = 0;
    static Platform* getPlatform() { ++lookups; return platform; }
    static NVGcontext* getNVGContext() {
        ++lookups;
        assert(platform && platform->video); // Old clean() crashes here.
        return platform->video->getNVGContext();
    }
    static Event<>* getWindowSizeChangedEvent() { return &resizeEvent; }
    static Event<>* getExitEvent() { return &exitEvent; }
};
}
#include <borealis/core/cache_helper.hpp>

int main() {
    auto& cache = brls::TextureCache::instance();
    cache.clean();
    brls::Application::exitEvent.fire();
    assert(brls::Application::lookups == 0); // Suspended, empty exit is safe.

    brls::Video first;
    brls::Platform platform;
    platform.video = &first;
    brls::Application::platform = &platform;
    cache.addCache("first", 11);
    cache.addCache("second", 12);
    cache.markDirty(11);
    cache.clean(); // Host releases window before destroying the GL context.
    assert(first.context.deleted.size() == 2);
    assert(cache.getCache("first") == 0 && cache.getCache("second") == 0);
    assert(cache.cache.getCacheList().empty());
    platform.video = nullptr;
    auto lookups = brls::Application::lookups;
    cache.clean();
    brls::Application::exitEvent.fire(); // Reproduces dglabNativeShutdown.
    assert(brls::Application::lookups == lookups);
    assert(first.context.deleted.size() == 2); // No repeated deletion.

    brls::Video second;
    platform.video = &second;
    cache.addCache("first", 11); // New context may reuse the same image ID.
    assert(cache.getCache("first") == 11);
    cache.removeCache(11);
    assert(cache.cache.getCacheList().front().count == 1); // Value index reset.
    brls::Application::exitEvent.fire(); // Exit with a live window also works.
    assert(second.context.deleted == std::vector<int>{11});
    assert(first.context.deleted.size() == 2);
    assert(cache.getCache("first") == 0);

    cache.addCache("lost-context", 13);
    platform.video = nullptr; // Partial initialization / already lost context.
    cache.clean();
    assert(cache.getCache("lost-context") == 0);
    assert(cache.cache.getCacheList().empty());
    brls::Application::platform = nullptr;
    brls::Application::exitEvent.fire();
    puts("Texture cache suspend/recreate/exit regression tests passed");
}
