# Borealis vendor snapshot

Source: https://github.com/dragonflylee/borealis/tree/f347bfd419ae28d466ca7b0957ca73b3c401e957
Branch at selection: switchfin. Imported 2026-10-10.

Only library/ and required resources are imported. The vendor build is for the
DGLAB-NX custom Switch platform, not the upstream demo. No FetchContent path is
used in this build. Bundled fmt, Tweeny, Yoga, tinyxml2, NanoVG, GLAD and
libretro-common come from that exact tree; keep their individual license notices.
Top-level Apache-2.0 LICENSE, NOTICE and AUTHORS are retained.
Yoga retains its C++ core, CMake helpers, README and licenses; unused mobile/JS
bindings, sample apps, websites, upstream tests and release/editor metadata are
omitted. DGLAB-NX and the host preview both build yoga/yoga directly.
The unused fmt/Tweeny documentation and test trees and nested upstream
CI/editor/subtree metadata are also omitted; dependency documentation/test
options remain disabled in the project builds.

The upstream notification manager .hpp/.cpp carry GPL-3.0-or-later notices despite
the top-level license. These two files are omitted and all notification references
are disabled with DGLAB_BOREALIS_MINIMAL. No upstream notification code is linked.
The old bundled Switch font dumps and demo assets are omitted. Fonts are acquired
from pl:u at runtime; MaterialIcons retains resources/material/LICENSE.txt.

Local changes:

- BRLS_CUSTOM_PLATFORM excludes the stock platform factory, Switch drivers,
  libpulsar and notifications. DGLAB-NX supplies platform/HID/font/IME/audio interfaces.
- Application::releaseWindowState clears UI callbacks, animations, activities,
  deferred deletion, focus and input. It optionally retains font handles for
  a native-page transition; backend handoff always clears them.
- Application::shutdown permits orderly teardown without another rendered frame.
  The normal applet exit also clears window state before destroying the platform.
  Failed initialization releases the partially created platform.
- Yoga event/XML view registration occurs once across window recreation;
  getDefaultFont no longer caches a handle from a previous NanoVG context.
- Threading::clearPending discards UI sync/delay callbacks. The host does not
  submit async UI tasks. Ticking::cancelAll cancels callbacks and resets the clock.
- GLFW dimensions are initialized; GL loader and NanoVG failures release their
  partial window and throw to the host's ASCII console diagnostic.
- The host compiles NanoVG's stb_truetype with STBTT_STATIC to avoid collisions
  with the existing CPU font implementation.
- TextureCache is emptied while the NanoVG context still exists on window
  release. Cleanup clears all LRU indices and tolerates repeated exit callbacks
  after suspension; texture IDs never survive a context replacement.
- Label::setFont allows explicit font selection. Under DGLAB_BOREALIS_MINIMAL,
  Hint binds its icon label to the host's NintendoExt font instead of the body
  font; BottomBar hides the clock and skips clock text updates. Matching left/right
  actions share a hint with both direction icons and one action label.

Source modifications are intentionally kept in place and documented here for
review against the pinned tree. Do not update the upstream revision implicitly.
