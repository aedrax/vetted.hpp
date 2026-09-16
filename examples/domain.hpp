// The validated types for this example, one line each.
//
// The example is a small online shop: orders, uploads, users, and server
// settings. List rules directly (they run in order and stop at the first
// failure), or bundle them with AllOf and Between.
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

constexpr int32_t kMaxChunkSize = 65536;

// FileOffset is bounded so that `offset + length` can never overflow int64_t.
// A rule promises exactly what it says. AtLeast<0> alone lets INT64_MAX
// through, and the addition in read_slice is then undefined behavior.
constexpr int64_t kMaxFileOffset = std::numeric_limits<int64_t>::max() - kMaxChunkSize;

using Quantity     = Validated<int16_t, Positive, AtMost<1000>>;   // items in one order line

// Gift wrap is offered for at most 5 items. A GiftQuantity is a Quantity with
// one more rule, so it is accepted wherever a Quantity is, with no re-check.
// The other direction is explicit: GiftQuantity::try_from(quantity).
using GiftQuantity = Quantity::With<AtMost<5>>;
using FileOffset   = Validated<int64_t, AtLeast<0>, AtMost<kMaxFileOffset>>;
using ChunkSize    = Validated<int32_t, PowerOfTwo, AtMost<kMaxChunkSize>>;  // read buffers
using ShippingDays = Validated<int32_t, In<1, 2, 5>>;                       // overnight, two-day, standard
using Percent      = Validated<int, Between<0, 100>>;

// A floor in the building. There is no floor 0 and no floor 13.
using Floor        = Validated<int, Between<-2, 50>, NotIn<0, 13>>;

// A coupon: a multiple of 5 percent up to 50, or exactly 100 (free).
// Named replaces the message the combinators would build ("a multiple of 5
// and <= 50, or one of {100}") with what it means.
using Coupon       = Validated<int, Named<AnyOf<AllOf<MultipleOf<5>, AtMost<50>>, In<100>>,
                                          "a multiple of 5 up to 50, or exactly 100">>;

// One type per rule in the toolbox, so each one shows up in the demo.
using SlotMinutes  = Validated<int32_t, Positive, MultipleOf<15>, AtMost<480>>;  // meeting length
using DiskOffset   = Validated<int64_t, NonNegative, Aligned<512>>;              // on a block boundary
using Step         = Validated<int32_t, NonZero>;                                // for a countdown, may be negative
using ColorChannel = Validated<int32_t, FitsInBits<8>>;                          // 0..255
using Permissions  = Validated<uint16_t, OnlyBits<0x7>>;                         // read, write, execute
using RowIndex     = Validated<int, AtLeast<0>, LessThan<256>>;                  // half-open [0, 256)
using Price        = Validated<double, Finite, GreaterThan<0.0>, AtMost<1000000.0>>;
using PlayerCount  = Validated<int, Satisfies<[](auto v) { return v == 1 || v % 2 == 0; },
                                               "1 or an even number">>;          // solo, or pairs
using RetryDelay   = Validated<int32_t, NonNegative>;                            // milliseconds
using ImageWidth   = Validated<int32_t, Positive, Even>;                         // video encoders need even sizes
using PanelSize    = Validated<int32_t, Positive, Odd>;                          // odd, so a vote cannot tie
using Temperature  = Validated<int32_t, FitsIn<int8_t>>;                         // parsed as int, stored in one byte
using SharePerms   = Validated<uint16_t, HasBits<0x1>, OnlyBits<0x7>>;           // bit 0 (read) must be set
using Locale       = Validated<const char*, NotNull>;

// Containers: any T with a size() that you can iterate. Text: any T that converts to string_view.
using Attachment   = Validated<std::vector<uint8_t>, NonEmpty, SizeAtMost<1024>>;
using Scores       = Validated<std::vector<int16_t>, NonEmpty, Each<Between<0, 100>>>;
using Milestones   = Validated<std::vector<Percent>, NonEmpty, Sorted, Unique>;  // elements already validated
using ListenPort   = Validated<uint16_t, PortNumber>;
using DisplayName  = Validated<std::string_view, NonEmpty, SizeAtMost<32>, Printable>;
using DeviceMac    = Validated<std::string_view, MacAddress>;
using SessionId    = Validated<std::string_view, Uuid>;
using Comment      = Validated<std::string, Utf8, SizeAtMost<140>>;
using CountryCode  = Validated<std::string_view, SizeIs<2>, OnlyChars<"ABCDEFGHIJKLMNOPQRSTUVWXYZ">>;
using Username     = Validated<std::string_view, SizeBetween<3, 16>,
                                                 OnlyChars<"abcdefghijklmnopqrstuvwxyz0123456789_">>;
using ReportName   = Validated<std::string_view, StartsWith<"report-">, Contains<"-q">, EndsWith<".pdf">>;
using ApiHost      = Validated<std::string_view, Hostname>;
using ServerAddress= Validated<std::string_view, IpAddress>;                     // IPv4 or IPv6
using ContactEmail = Validated<std::string_view, EmailAddress>;
using WebhookUrl   = Validated<std::string_view, Url, StartsWith<"https://">>;

// Validated types compose into structs like any other member. An Order can
// only be built from a valid Quantity, ShippingDays and Percent, so the whole
// struct carries the same guarantee. If you have one, every field passed.
struct Order {
    Quantity     quantity;
    ShippingDays shipping;
    Percent      discount;
};

// No hidden cost. The struct has the same layout as one with raw int fields.
struct OrderRaw { int16_t quantity; int32_t shipping; int discount; };
static_assert(sizeof(Order) == sizeof(OrderRaw));
static_assert(alignof(Order) == alignof(OrderRaw));
static_assert(std::is_trivially_copyable_v<Order>);

// A rule can span several fields. Validated<T> works for any T, so wrap a
// struct and write a rule that reads the whole thing. Here, each field is
// valid on its own, but the pair must also fit inside the largest file the
// service stores. Neither FileOffset nor ChunkSize can promise that alone.
constexpr int64_t kMaxFileBytes = int64_t{1} << 20;

struct FileRange {
    FileOffset offset;
    ChunkSize  length;
};

struct WithinFile {
    static constexpr bool passes(const FileRange& r) {
        return r.offset + r.length <= kMaxFileBytes;  // cannot overflow: see kMaxFileOffset
    }
    static std::string requirement() {
        return "offset + length <= " + std::to_string(kMaxFileBytes);
    }
};

using Slice = Validated<FileRange, WithinFile>;
