#pragma once
extern "C" {
#include <dglab/ui/native.h>
}
#include <borealis/views/applet_frame.hpp>
#include <memory>

namespace dglab {
// Framework owns the frame and child views; this adapter owns only copied state.
class Gameplay {
public:
    explicit Gameplay(DglabMenuResult page);
    ~Gameplay();
    brls::AppletFrame* frame() const;
    void update(const DglabGameplayState& state);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
