// The validated types for this example, one line each.
//
// List rules directly (they run in order and stop at the first failure), or
// bundle them with AllOf and Between.
#pragma once

#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

#include <vetted.hpp>

// Fine for an example. A real project should qualify names (vetted::Validated)
// or pull in only the names it uses.
using namespace vetted;

constexpr int32_t kMaxFFTSize = 65536;

// BlockOffset is bounded so that `offset + size` can never overflow int64_t.
// A rule promises exactly what it says. AtLeast<0> alone lets INT64_MAX
// through, and the addition in process_iq_block is then undefined behavior.
constexpr int64_t kMaxBlockOffset = std::numeric_limits<int64_t>::max() - kMaxFFTSize;

using ChannelID   = Validated<int16_t, Positive, AtMost<4096>>;

// Channels 1..100 are the VHF band. A VhfChannel is a ChannelID with one more
// rule, so it is accepted wherever a ChannelID is, with no re-check. The other
// direction is explicit: VhfChannel::try_from(channel).
using VhfChannel  = ChannelID::With<AtMost<100>>;
using BlockOffset = Validated<int64_t, AtLeast<0>, AtMost<kMaxBlockOffset>>;
using FFTSize     = Validated<int32_t, PowerOfTwo, AtMost<kMaxFFTSize>>;
using Baud        = Validated<int32_t, In<9600, 19200, 38400, 115200>>;
using Percent     = Validated<int, Between<0, 100>>;

// A GPIO pin, 0..31, except the pins wired to the boot strap and the UART.
using GpioPin     = Validated<int, Between<0, 31>, NotIn<0, 1, 14, 15>>;

// A DMA burst: either a small power of two, or exactly the hardware maximum.
// Named replaces the message the combinators would build ("a power of two
// and <= 64, or one of {1000}") with what it means.
using BurstLength = Validated<int, Named<AnyOf<AllOf<PowerOfTwo, AtMost<64>>, In<1000>>,
                                         "a power of two up to 64, or exactly 1000">>;

// One type per rule in the toolbox, so each shows up in the demo.
using DmaLength   = Validated<int32_t, NonZero, Aligned<4>, AtMost<65536>>;  // bytes, 4-byte aligned
using Stride      = Validated<int32_t, NonZero>;                           // may be negative
using DacValue    = Validated<int32_t, FitsInBits<12>>;                    // 12-bit DAC register
using LedMask     = Validated<uint16_t, OnlyBits<0x0F>>;                   // four LEDs, bits 0..3
using BufferIndex = Validated<int, AtLeast<0>, LessThan<256>>;             // half-open [0, 256)
using Gain        = Validated<double, Finite, GreaterThan<0.0>, AtMost<10.0>>;
using PllDivider  = Validated<int, Satisfies<[](auto v) { return v == 1 || v % 2 == 0; },
                                              "1 or an even number">>;
using Delay       = Validated<int32_t, NonNegative>;                       // microseconds
using IQCount     = Validated<int32_t, Positive, Even>;                    // I and Q come in pairs
using TapCount    = Validated<int32_t, Positive, Odd>;                     // symmetric FIR filter
using TrimOffset  = Validated<int32_t, FitsIn<int8_t>>;                    // read as int32, stored in an int8 register
using CtrlWord    = Validated<uint16_t, HasBits<0x01>, OnlyBits<0x0F>>;    // bit 0 is enable and must be set
using DeviceName  = Validated<const char*, NotNull>;

// Containers: any T with a size() that you can iterate. Text: any T that converts to string_view.
using Payload     = Validated<std::vector<uint8_t>, NonEmpty, SizeAtMost<256>>;
using Samples     = Validated<std::vector<int16_t>, NonEmpty, Each<Between<-2048, 2047>>>;  // 12-bit ADC
using ScanList    = Validated<std::vector<ChannelID>, NonEmpty, Sorted, Unique>;  // elements already validated
using StreamPort  = Validated<uint16_t, PortNumber>;
using Label       = Validated<std::string_view, NonEmpty, SizeAtMost<16>, Printable>;  // fits the front panel
using DeviceMac   = Validated<std::string_view, MacAddress>;
using SessionId   = Validated<std::string_view, Uuid>;
using Comment     = Validated<std::string, Utf8, SizeAtMost<140>>;
using CountryCode = Validated<std::string_view, SizeIs<2>, OnlyChars<"ABCDEFGHIJKLMNOPQRSTUVWXYZ">>;
using Callsign    = Validated<std::string_view, SizeBetween<3, 8>,
                                                OnlyChars<"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789">>;
using Firmware    = Validated<std::string_view, StartsWith<"sdr-">, Contains<"-v">, EndsWith<".bin">>;
using ControlHost = Validated<std::string_view, Hostname>;
using StreamTarget= Validated<std::string_view, IpAddress>;                // IPv4 or IPv6
using Operator    = Validated<std::string_view, EmailAddress>;
using UpdateUrl   = Validated<std::string_view, Url, StartsWith<"https://">>;

// Validated types compose into structs like any other member. A RadioConfig
// can only be built from a valid ChannelID, Baud and Percent, so the whole
// struct carries the same guarantee. If you have one, every field passed.
struct RadioConfig {
    ChannelID channel;
    Baud      baud;
    Percent   volume;
};

// No hidden cost. The struct has the same layout as one with raw int fields.
struct RadioConfigRaw { int16_t channel; int32_t baud; int volume; };
static_assert(sizeof(RadioConfig) == sizeof(RadioConfigRaw));
static_assert(alignof(RadioConfig) == alignof(RadioConfigRaw));
static_assert(std::is_trivially_copyable_v<RadioConfig>);

// A rule can span several fields. Validated<T> works for any T, so wrap a
// struct and write a rule that reads the whole thing. Here, each field is
// valid on its own, but the pair must also fit inside the capture buffer.
// Neither BlockOffset nor FFTSize can promise that alone.
constexpr int64_t kCaptureBufferSamples = int64_t{1} << 20;

struct IQRange {
    BlockOffset offset;
    FFTSize     size;
};

struct WithinCaptureBuffer {
    static constexpr bool passes(const IQRange& r) {
        return r.offset + r.size <= kCaptureBufferSamples;  // cannot overflow: see kMaxBlockOffset
    }
    static std::string requirement() {
        return "offset + size <= " + std::to_string(kCaptureBufferSamples);
    }
};

using IQBlock = Validated<IQRange, WithinCaptureBuffer>;
