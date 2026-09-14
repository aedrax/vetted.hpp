# Writing and combining rules

The snippets below assume `using namespace vetted;`, as the example code does.

A rule is a struct with two static functions. `passes` says whether a value
passes, and `requirement` states the condition for the error message.
`requirement` returns the bare condition ("a power of two", "<= 4096");
`Validated` prefixes it with "expected" when it builds the message, so
combinators can nest without the wording piling up.

```cpp
struct PowerOfTwo {
    static constexpr bool passes(auto v) { return v > 0 && (v & (v - 1)) == 0; }
    static std::string requirement() { return "a power of two"; }
};
```

Rules can take parameters:

```cpp
template <auto Max>
struct AtMost {
    static constexpr bool passes(auto v) { return v <= Max; }
    static std::string requirement() { return "<= " + std::to_string(Max); }
};
```

## Chaining rules

List them, and they run in order:

```cpp
using FFTSize = Validated<int32_t, PowerOfTwo, AtMost<65536>>;
```

Or bundle them into a new rule so the intent has a name:

```cpp
template <auto Lo, auto Hi>
using Between = AllOf<AtLeast<Lo>, AtMost<Hi>>;

using Percent = Validated<int, Between<0, 100>>;
```

`AllOf`, `AnyOf` and `Not` nest however you like, so odd hardware constraints
still read as one line:

```cpp
// 0..31, except the pins reserved for boot and the UART
using GpioPin     = Validated<int, Between<0, 31>, NotIn<0, 1, 14, 15>>;

// a small power of two, or exactly the hardware maximum
using BurstLength = Validated<int, AnyOf<AllOf<PowerOfTwo, AtMost<64>>, In<1000>>>;
```

A nested combinator builds its message from its parts, so `BurstLength{96}`
says "expected a power of two and <= 64, or one of {1000}". Wrap it in
`Named` to keep the check and say what it means:

```cpp
using BurstLength = Validated<int, Named<AnyOf<AllOf<PowerOfTwo, AtMost<64>>, In<1000>>,
                                         "a power of two up to 64, or exactly 1000">>;
```

## Rules over containers and text

`Validated<T>` is not limited to numbers. The container rules work for any
`T` with a `size()` you can iterate, and the text rules for any `T` that
converts to `std::string_view`:

```cpp
using Payload  = Validated<std::vector<uint8_t>, NonEmpty, SizeAtMost<256>>;
using Samples  = Validated<std::vector<int16_t>, NonEmpty, Each<Between<-2048, 2047>>>;
using ScanList = Validated<std::vector<ChannelID>, NonEmpty, Sorted, Unique>;
using Callsign = Validated<std::string_view, SizeBetween<3, 8>,
                                             OnlyChars<"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789">>;
using Target   = Validated<std::string_view, IpAddress>;   // IPv4 or IPv6
```

`Each<R>` takes a rule type, so the whole toolbox applies to elements. The
elements can be validated types themselves, as in `ScanList`: each
`ChannelID` already passed its own rules, and the list rule adds an order.

The text rules run at compile time too, so a constant address or URL that is
malformed fails the build:

```cpp
constexpr Target loopback{"::1"};      // fine
constexpr Target oops{"2001:db8:::1"}; // error: rule_violated<AnyOf<Ipv4Address, Ipv6Address>>
```

`Hostname`, `Ipv4Address`, `Ipv6Address`, `EmailAddress`, `Url`, `MacAddress`
and `Uuid` check the shape of the text and nothing more. They do not resolve names or implement
every corner of the RFCs. `EmailAddress` accepts `local@domain` with the
characters RFC 5322 allows unquoted and a domain with at least one dot. It
rejects quoted local parts and IP-literal domains, which almost nothing uses.
`Url` accepts a scheme, a colon and at least one more character, with no
whitespace. If you need a full parser, write a rule around one: `passes` can
call anything.

For a `const char*`, put `NotNull` first so the text rules never read
through a null pointer.

## Rules over structs

`Validated<T>` works for any `T`, so a rule can look at several fields at
once. See [structs.md](structs.md) for `IQBlock`, whose rule checks that an
offset and a size fit inside a buffer together.

## Error messages for other types

The error message prints the value when `std::to_string` can (numbers) and
quotes it when it is text (`std::string`, `std::string_view`). For a struct,
a container or a pointer the message says just "value". Swap that in
`describe_value` for whatever formatting makes sense.
