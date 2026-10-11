#pragma once

// Coyote V3 device session: turns user input and waveform data into the byte
// packets the device expects, at the 100ms cadence the protocol requires.
//
// This layer still has no BLE dependency. It writes packets through a
// DglabCoyoteV3Link callback, so the sysmodule can plug in its BLE transport
// later while the host tests plug in a recorder. Timing is driven from the
// outside through dglabCoyoteV3SessionTick(), which keeps the logic free of
// threads and clock sources and therefore testable.
//
// Source of truth:
//   https://github.com/dungeonlab-open/dglab-bluetooth-protocol
//   coyote/v3/README.md @ 0155a23cfc123d1243b96482ea475d147573ce8f

#include <dglab/protocol/coyote_v3.h>
#include <dglab/protocol/coyote_v3_strength.h>

#include <stddef.h>
#include <stdint.h>

// Waveform entries a channel can hold. 128 entries cover 3.2s of playback.
#define DGLAB_COYOTE_V3_WAVEFORM_CAPACITY 128u

// The protocol expects one B0 packet every 100ms.
#define DGLAB_COYOTE_V3_OUTPUT_INTERVAL_MS 100u

// App side waveform frequency range, before compression to the device range.
#define DGLAB_COYOTE_V3_WAVEFORM_FREQUENCY_MIN_MS 10u
#define DGLAB_COYOTE_V3_WAVEFORM_FREQUENCY_MAX_MS 1000u

// One app side waveform step. It covers 25ms of output once placed in a packet.
typedef struct {
    uint16_t frequency_ms;
    uint8_t strength;
} DglabCoyoteV3WaveformEntry;

// Transport hooks. The session never closes or owns the link; it only writes.
typedef struct {
    void (*write)(void* context, const uint8_t* data, size_t size);
    void* context;
} DglabCoyoteV3Link;

typedef struct {
    DglabCoyoteV3WaveformEntry entries[DGLAB_COYOTE_V3_WAVEFORM_CAPACITY];
    size_t count;
    size_t position;
} DglabCoyoteV3WaveformChannel;

typedef struct {
    // Soft limits and balance parameters, written after every connect.
    DglabCoyoteV3Bf bf;
    // Strength write policy. DglabCoyoteV3StrengthFeedback_Required follows the
    // reference algorithm in the documentation and is the default, because it is
    // the enum's zero value.
    DglabCoyoteV3StrengthFeedback strength_feedback;
    // Play each uploaded batch once and write B0 only for waveform or strength
    // input. False preserves the diagnostic probe's continuous, looping stream.
    bool output_on_demand;
} DglabCoyoteV3SessionConfig;

typedef struct {
    DglabCoyoteV3Link link;
    DglabCoyoteV3Bf bf;
    DglabCoyoteV3StrengthFeedback strength_feedback;
    DglabCoyoteV3StrengthState strength;
    DglabCoyoteV3WaveformChannel waveform_a;
    DglabCoyoteV3WaveformChannel waveform_b;
    bool connected;
    bool output_on_demand;
    uint32_t elapsed_ms;
} DglabCoyoteV3Session;

void dglabCoyoteV3SessionInit(DglabCoyoteV3Session* session, const DglabCoyoteV3Link* link,
    const DglabCoyoteV3SessionConfig* config);

// Marks the device as connected, resets per connection state and immediately
// writes the BF packet. Writing BF first guarantees the soft limits are known
// before any B0 packet can change the strength.
void dglabCoyoteV3SessionOnConnected(DglabCoyoteV3Session* session);

// Marks the device as disconnected and drops per connection state (in flight
// strength changes, waveform playback position, tick remainder). Configured
// waveform data and BF parameters are kept for the next connection.
void dglabCoyoteV3SessionOnDisconnected(DglabCoyoteV3Session* session);

bool dglabCoyoteV3SessionIsConnected(const DglabCoyoteV3Session* session);

// Feeds a BLE notification (B1) into the session. Anything that is not a valid
// B1 is ignored. This is what releases the strength gate.
void dglabCoyoteV3SessionOnNotification(DglabCoyoteV3Session* session, const uint8_t* data,
    size_t size);

// Accumulates a relative strength change. Ignored while disconnected.
void dglabCoyoteV3SessionAdjustStrength(DglabCoyoteV3Session* session,
    DglabCoyoteV3Channel channel, int32_t delta);

// Requests an absolute zero on the next allowed packet. Ignored while
// disconnected.
void dglabCoyoteV3SessionSetStrengthZero(DglabCoyoteV3Session* session,
    DglabCoyoteV3Channel channel);

// Sets the waveform a channel plays. Entries are sent in order, four per B0
// packet. In output_on_demand mode the batch plays once; otherwise it repeats
// until cleared. Returns false and
// leaves the current waveform untouched when count is 0, larger than
// DGLAB_COYOTE_V3_WAVEFORM_CAPACITY, or when any entry is out of range.
bool dglabCoyoteV3SessionSetWaveform(DglabCoyoteV3Session* session,
    DglabCoyoteV3Channel channel, const DglabCoyoteV3WaveformEntry* entries, size_t count);

// Appends to the unplayed entries of an on-demand channel. Invalid input or a
// full queue leaves playback untouched. Not supported for looping sessions.
bool dglabCoyoteV3SessionAppendWaveform(DglabCoyoteV3Session* session,
    DglabCoyoteV3Channel channel, const DglabCoyoteV3WaveformEntry* entries, size_t count);

// Stops playing a channel's waveform. The channel is written as idle data,
// which the device discards, so the other channel keeps playing.
void dglabCoyoteV3SessionClearWaveform(DglabCoyoteV3Session* session,
    DglabCoyoteV3Channel channel);

// Advances time. Every completed 100ms interval may write one B0 packet.
// output_on_demand sessions stay silent when no waveform or strength is queued.
//
// At most one packet is written per call: a caller that arrives late must not
// turn the missed intervals into a burst of writes. The sub interval remainder
// is kept, so a caller ticking every 10ms stays in phase.
void dglabCoyoteV3SessionTick(DglabCoyoteV3Session* session, uint32_t elapsed_ms);
