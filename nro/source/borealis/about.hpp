#pragma once
extern "C" {
#include <dglab/ui/about.h>
}
#include <borealis/core/box.hpp>

namespace dglab {
// Explanatory prose followed by read-only framework list rows.
void buildAboutContent(brls::Box* content, const DglabAboutState& state);
}
