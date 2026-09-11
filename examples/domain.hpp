// This example's validated types, one line each
//
// Rules can be listed directly (they run in order and stop at the first
// failure) or bundled with AllOf/Between.
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include <vetted.hpp>

// An example is allowed this. A real project would qualify (vetted::Validated)
// or pull in just the names it uses.
using namespace vetted;

constexpr int32_t kMaxFFTSize = 65536;

// BlockOffset is bounded so that `offset + size` can never overflow int64_t.
// A rule promises exactly what it says: AtLeast<0> alone would let INT64_MAX
// through, and process_iq_block's addition would be undefined behaviour.
constexpr int64_t kMaxBlockOffset = std::numeric_limits<int64_t>::max() - kMaxFFTSize;

using ChannelID   = Validated<int16_t, Positive, AtMost<4096>>;
using BlockOffset = Validated<int64_t, AtLeast<0>, AtMost<kMaxBlockOffset>>;
using FFTSize     = Validated<int32_t, PowerOfTwo, AtMost<kMaxFFTSize>>;
using Baud        = Validated<int32_t, In<9600, 19200, 38400, 115200>>;
using Percent     = Validated<int, Between<0, 100>>;

// A GPIO pin, 0..31, except the pins wired to the boot strap and the UART.
using GpioPin     = Validated<int, Between<0, 31>, NotIn<0, 1, 14, 15>>;

// A DMA burst: either a small power of two, or exactly the hardware maximum.
using BurstLength = Validated<int, AnyOf<AllOf<PowerOfTwo, AtMost<64>>, In<1000>>>;

// One type per rule in the toolbox, so each shows up in the demo.
using DmaLength   = Validated<int32_t, NonZero, Aligned<4>, AtMost<65536>>;  // bytes, 4-byte aligned
using Stride      = Validated<int32_t, NonZero>;                           // may be negative
using DacValue    = Validated<int32_t, FitsInBits<12>>;                    // 12-bit DAC register
using LedMask     = Validated<uint16_t, OnlyBits<0x0F>>;                   // four LEDs, bits 0..3
using BufferIndex = Validated<int, AtLeast<0>, LessThan<256>>;             // half-open [0, 256)
using Gain        = Validated<double, Finite, GreaterThan<0.0>, AtMost<10.0>>;
using PllDivider  = Validated<int, Satisfies<[](auto v) { return v == 1 || v % 2 == 0; },
                                              "1 or an even number">>;

// Validated types compose into structs like any other member. A RadioConfig
// can only be built from a valid ChannelID, Baud and Percent, so the struct
// as a whole carries the same guarantee: if you have one, every field passed.
struct RadioConfig {
    ChannelID channel;
    Baud      baud;
    Percent   volume;
};

// No hidden cost: the struct is laid out exactly as if the fields were raw ints.
struct RadioConfigRaw { int16_t channel; int32_t baud; int volume; };
static_assert(sizeof(RadioConfig) == sizeof(RadioConfigRaw));
static_assert(alignof(RadioConfig) == alignof(RadioConfigRaw));
static_assert(std::is_trivially_copyable_v<RadioConfig>);

// A rule can span several fields. Validated<T> works for any T, so wrap a
// struct and write a rule that looks at the whole thing. Here, each field is
// valid on its own, but the pair must also fit inside the capture buffer.
// Neither BlockOffset nor FFTSize can promise that alone.
constexpr int64_t kCaptureBufferSamples = int64_t{1} << 20;

struct IQRange {
    BlockOffset offset;
    FFTSize     size;
};

struct WithinCaptureBuffer {
    static constexpr bool passes(const IQRange& r) {
        return r.offset + r.size <= kCaptureBufferSamples;  // can't overflow: see kMaxBlockOffset
    }
    static std::string requirement() {
        return "offset + size <= " + std::to_string(kCaptureBufferSamples);
    }
};

using IQBlock = Validated<IQRange, WithinCaptureBuffer>;
