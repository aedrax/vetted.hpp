<p align="center">
  <img src="./assets/readme/hero.svg" width="100%" alt="vetted.hpp: a single-header C++20 library for validated types. Check a value once, where it enters; the type carries the proof after that. A raw value that passes becomes a ChannelID and flows through three functions with no re-check; one that fails is rejected at the boundary.">
</p>

A `vetted::Validated<T, Rules...>` is a `T` that passes every rule. The only
way to make one is a constructor that runs the rules, so a function that
receives one can use the value without a check. You validate once, where
untrusted data enters. The type carries the proof through every layer below.

## The problem it removes

Say `handle_request()` calls `tune_radio()`, which calls `write_register()`,
and all three take a radio channel number. If that number is a plain
`int16_t`, none of them knows whether the caller checked it. So they all
check:

```cpp
void write_register(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    ...
}
void tune_radio(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    write_register(channel);
}
void handle_request(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    tune_radio(channel);
}
```

That is three copies of one rule. A fourth layer means a fourth copy. Change
the rule and you must find them all.

Instead, make "a checked channel number" its own type:

```cpp
using ChannelID = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<4096>>;

void write_register(ChannelID channel) { ... }
void tune_radio(ChannelID channel)     { write_register(channel); }
void handle_request(ChannelID channel) { tune_radio(channel); }
```

No checks anywhere. Validation is visible in one place: where untrusted data
becomes a `ChannelID`.

```cpp
std::optional<ChannelID> parse_channel(std::string_view text) {
    int16_t raw = /* parse the digits */;
    return ChannelID::try_from(raw);   // nullopt if any rule fails
}
```

When the value is a constant, the compiler runs the rules at build time and
names the rule that failed:

```
constexpr ChannelID oops{5000};
// error: constexpr variable 'oops' must be initialized by a constant expression
// note: non-constexpr function 'rule_violated<vetted::AtMost<4096>>' cannot be used in a constant expression
```

## Is it safer? Is it faster?

Safer, yes. A missing check is a compile error. The rule lives on one line.
Every place a raw value becomes a trusted one is greppable.

Faster, a little. The wrapper is the same size as the raw type and passes in
registers. A validated call chain drops the range check from every layer.
See [docs/safety-and-performance.md](docs/safety-and-performance.md) for the
measurements and the caveats.

## Using it

Copy `include/vetted.hpp` into your project, or add it with CMake:

```cmake
include(FetchContent)
FetchContent_Declare(vetted GIT_REPOSITORY https://github.com/aedrax/vetted.hpp.git GIT_TAG main)
FetchContent_MakeAvailable(vetted)

target_link_libraries(app PRIVATE vetted::vetted)
```

Or install it (`cmake --install build --prefix /some/where`) and use
`find_package(vetted REQUIRED)`. Either way the target is `vetted::vetted`,
and it sets C++20 for you.

The snippets below assume `using namespace vetted;`, as the example code does.

## How it works

<p align="center">
  <img src="./assets/readme/anatomy.svg" width="100%" alt="Anatomy of the declaration Validated of int16_t, Positive, AtMost 4096: int16_t is the value type and reads back as a plain int16_t; Positive and AtMost are rules that run in order, and each rule is a struct with a passes function and a requirement function.">
</p>

A rule is a struct with two static functions. `passes` returns whether a
value passes. `requirement` returns the condition for the error message:

```cpp
struct PowerOfTwo {
    static constexpr bool passes(auto v) { return v > 0 && (v & (v - 1)) == 0; }
    static std::string requirement() { return "a power of two"; }
};
```

`Validated` uses a C++20 concept to check that every rule has both functions.
This costs nothing at runtime. If you forget `requirement()`, the build stops
at the `using` line:

```
error: constraints not satisfied for class template 'Validated' [with T = int, Rules = <Even>]
note: because 'Rule::requirement()' would be invalid: no member named 'requirement' in 'Even'
```

### Two ways in

| | When | On failure |
|---|---|---|
| `ChannelID{v}` | The value must never be wrong: constants, config, computed values | Throws `std::invalid_argument`, for example `value 5000: expected <= 4096`. In a `constexpr` context, the build fails instead. |
| `ChannelID::try_from(v)` | Untrusted input: user text, network, files | Returns `std::nullopt` |

## The toolbox

`vetted.hpp` ships with these rules. Most are three lines, so add your own.

| Rule | Passes when | Typical use |
|---|---|---|
| `Positive` | `v > 0` | counts, sizes |
| `NonNegative` | `v >= 0` | offsets, delays |
| `NonZero` | `v != 0` | divisors, strides (negative allowed) |
| `Even`, `Odd` | `v % 2 == 0`, `v % 2 != 0` | sample pairs, filter taps |
| `AtLeast<N>`, `AtMost<N>` | `v >= N`, `v <= N` | closed ranges |
| `GreaterThan<N>`, `LessThan<N>` | `v > N`, `v < N` | open ranges, `[0, size)` |
| `Between<Lo, Hi>` | both bounds inclusive | percentages, channels |
| `In<a, b, c>` | `v` equals one of them | baud rates, enum-like ints |
| `NotIn<a, b, c>` | `v` equals none of them | reserved pins |
| `PowerOfTwo` | one bit set | FFT sizes, buffer sizes |
| `MultipleOf<N>`, `Aligned<N>` | `v % N == 0` | DMA lengths, addresses |
| `FitsInBits<N>` | `0 <= v < 2^N` | register fields |
| `FitsIn<U>` | `v` is representable in integer type `U` | a safe narrowing cast, `FitsIn<int8_t>` |
| `OnlyBits<Mask>` | no bits set outside `Mask` | flag words |
| `HasBits<Mask>` | every bit in `Mask` set | a required enable bit |
| `Finite` | not NaN, not infinity | any float from outside. Put it first. |
| `NotNull` | `p != nullptr` | raw and copyable smart pointers. Put it first. |
| `PortNumber` | `1 <= v <= 65535` | with `Hostname` or `IpAddress` |
| `AllOf<R...>`, `AnyOf<R...>`, `Not<R>` | combine other rules | anything the rules above cannot say alone |
| `Named<R, "text">` | `R` passes; the message says `"text"` | a readable message for a nested combinator |
| `Satisfies<lambda, "text">` | the lambda returns true | one-off rules that do not deserve a struct |

For containers, any `T` with a `size()` that you can iterate: strings,
vectors, arrays, spans.

| Rule | Passes when |
|---|---|
| `NonEmpty` | `size() != 0` |
| `SizeIs<N>`, `SizeAtLeast<N>`, `SizeAtMost<N>` | `size()` compared with `N` |
| `SizeBetween<Lo, Hi>` | both bounds inclusive |
| `Each<R>` | every element passes `R`, for example `Each<Between<-2048, 2047>>` |
| `Sorted` | ascending, equal neighbours allowed |
| `Unique` | no two elements equal |

For text, any `T` that converts to `std::string_view`. The named shapes check
the form of the text, at compile time when it is a constant. They do not
resolve names and do not cover every corner of the RFCs. The header states
exactly what each rule accepts.

| Rule | Passes when |
|---|---|
| `StartsWith<"...">`, `EndsWith<"...">`, `Contains<"...">` | the text has that prefix, suffix or part |
| `OnlyChars<"...">` | every character is in the set |
| `Printable` | every character is `0x20..0x7E`: no control characters, nothing outside ASCII |
| `Utf8` | well-formed UTF-8. Also takes a container of bytes. |
| `Hostname` | dot-separated labels of letters, digits and hyphens. `localhost` passes. |
| `Ipv4Address`, `Ipv6Address`, `IpAddress` | four dotted octets; eight hex groups with one optional `::`; either |
| `EmailAddress` | `local@domain` with at least one dot in the domain |
| `Url` | a scheme, a colon, and something after it. No whitespace. |
| `MacAddress` | six hex pairs, all `:` or all `-` separated |
| `Uuid` | `8-4-4-4-12` hex digits, any case |

```cpp
using UpdateUrl = Validated<std::string_view, Url, StartsWith<"https://">>;
constexpr UpdateUrl default_update{"https://example.org/fw"};   // the compiler checks the shape
```

`Satisfies` is the escape hatch. It makes a rule from a lambda:

```cpp
using PllDivider = Validated<int,
    Satisfies<[](auto v) { return v == 1 || v % 2 == 0; }, "1 or an even number">>;
```

### Chaining rules

List rules. They run in order and stop at the first failure:

```cpp
using FFTSize = Validated<int32_t, PowerOfTwo, AtMost<65536>>;
```

Rules can take template parameters, bundle into named combinations like
`Between<Lo, Hi>`, and nest with `AllOf`, `AnyOf` and `Not`. See
[docs/rules.md](docs/rules.md).

## Validated types in structs

Validated types compose like any other member. A struct of validated fields
carries the same guarantee as its parts. Its layout is identical to the raw
version. A rule can span several fields. Keep wire formats raw and convert
once at the boundary. See [docs/structs.md](docs/structs.md).

## Refining a type

A type can take more rules without a repeat of the rules it has:

```cpp
using ChannelID  = Validated<int16_t, Positive, AtMost<4096>>;
using VhfChannel = ChannelID::With<AtMost<100>>;
```

A `VhfChannel` is accepted wherever a `ChannelID` is, with no conversion
written and no re-check, because its rules include every `ChannelID` rule.
The other direction is explicit, like construction from a raw value, and runs
only the rule a `ChannelID` did not already prove:

```cpp
void tune_vhf(VhfChannel channel) { write_register(channel); }   // takes a ChannelID: fine

if (auto vhf = VhfChannel::try_from(channel)) tune_vhf(*vhf);    // checks AtMost<100> only
```

Rules are matched by type, so `Between<0, 100>` and the pair `AtLeast<0>,
AtMost<100>` are different rules. The order of rules does not affect
conversion.

## Limits

- The guarantee is only as good as the rules. Test them.
- A rule promises exactly what it says and no more. If a function needs
  `offset + size` to fit, the type must promise that, not only `>= 0`.
- Arithmetic on a validated value gives a plain `T`, so the proof does not
  survive math. Reading out is free. Only going in is guarded.
- It says "this number is in range" and nothing else. It is not memory safety
  or thread safety.

## The example

`examples/` holds a small radio-control program that uses everything above:
the call chain before and after, the parsing boundary, structs of validated
fields, and one line per rule in the toolbox.

```sh
cmake -S . -B build && cmake --build build && ./build/vetted_example
```

To watch the compiler reject a bad constant:

```sh
cmake -S . -B build -DDEMO_COMPILE_ERROR=ON && cmake --build build
```

| Path | What is in it |
|---|---|
| `include/vetted.hpp` | The whole library. Everything is in `namespace vetted`. |
| `examples/domain.hpp` | The example's types: `ChannelID`, `FFTSize`, `Baud`, ... one line each, plus structs of them |
| `examples/main.cc` | The call chain before and after, the parsing boundary, and the demo |
| `docs/` | Rules in depth, validated types in structs, and the safety and performance notes |

## License

MIT. See [LICENSE](LICENSE).
