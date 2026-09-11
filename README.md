# vetted.hpp

A single-header C++20 library for validated types. The idea is to check a
value once, where it enters the program, and let the type carry the proof from
then on.

```cpp
#include <vetted.hpp>

using ChannelID = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<4096>>;

void tune_radio(ChannelID channel);   // can't be called with an unchecked number
```

| Path | What's in it |
|---|---|
| `include/vetted.hpp` | The whole library: the `Validated<T, Rules...>` wrapper and the rule toolbox. Everything is in `namespace vetted`. |
| `examples/domain.hpp` | An example project's types: `ChannelID`, `FFTSize`, `Baud`, ... one line each, plus structs of them |
| `examples/main.cc` | The call chain before and after, the parsing boundary, and a demo |

## Using it

Copy `include/vetted.hpp` into your project, or add it with CMake:

```cmake
include(FetchContent)
FetchContent_Declare(vetted GIT_REPOSITORY https://github.com/aedrax/vetted.hpp.git GIT_TAG main)
FetchContent_MakeAvailable(vetted)

target_link_libraries(app PRIVATE vetted::vetted)
```

Or install it (`cmake --install build --prefix /some/where`) and use
`find_package(vetted REQUIRED)`. Either way the target is `vetted::vetted` and
it sets C++20 for you.

The snippets below assume `using namespace vetted;`, as the example code does.

## The problem

Say `handle_request()` calls `tune_radio()`, which calls `write_register()`,
and all three take a radio channel number. If that number is a plain
`int16_t`, none of them knows whether the caller already checked it. So they
all check:

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

That is three copies of the same rule, and a fourth layer would mean a fourth
copy. Change the rule and you have to go find them all.

## The fix

Make "a channel number that has been checked" its own type:

```cpp
using ChannelID = Validated<int16_t, Positive, AtMost<4096>>;
```

The only way to get a `ChannelID` is through a constructor that runs the
rules. So if a function *has* one, the checks already happened. The chain
becomes:

```cpp
void write_register(ChannelID channel) { ... }
void tune_radio(ChannelID channel)     { write_register(channel); }
void handle_request(ChannelID channel) { tune_radio(channel); }
```

No checks anywhere. Validation lives in exactly one place: wherever untrusted
data first turns into a `ChannelID`.

```cpp
std::optional<ChannelID> parse_channel(std::string_view text) {
    int16_t raw = /* parse the digits */;
    return ChannelID::try_from(raw);   // nullopt if any rule fails
}
```

## Writing a rule

A rule is a struct with two static functions. `passes` says whether a value
passes, and `requirement` states the condition for the error message:

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

`Validated` checks that every rule really has both functions, using a C++20
concept. It's a compile-time contract with no runtime cost. Forget `requirement()`
and the build stops at the `using` line:

```
error: constraints not satisfied for class template 'Validated' [with T = int, Rules = <Even>]
note: because 'Rule::requirement()' would be invalid: no member named 'requirement' in 'Even'
```

## The toolbox

`vetted.hpp` ships with these. Every one is three lines, so add your own freely.

| Rule | Passes when | Typical use |
|---|---|---|
| `Positive` | `v > 0` | counts, sizes |
| `NonZero` | `v != 0` | divisors, strides (negative allowed) |
| `AtLeast<N>`, `AtMost<N>` | `v >= N`, `v <= N` | closed ranges |
| `GreaterThan<N>`, `LessThan<N>` | `v > N`, `v < N` | open ranges, `[0, size)` |
| `Between<Lo, Hi>` | both bounds inclusive | percentages, channels |
| `In<a, b, c>` | `v` equals one of them | baud rates, enum-like ints |
| `NotIn<a, b, c>` | `v` equals none of them | reserved pins |
| `PowerOfTwo` | one bit set | FFT sizes, buffer sizes |
| `MultipleOf<N>`, `Aligned<N>` | `v % N == 0` | DMA lengths, addresses |
| `FitsInBits<N>` | `0 <= v < 2^N` | register fields |
| `OnlyBits<Mask>` | no bits set outside `Mask` | flag words |
| `Finite` | not NaN, not infinity | any float from outside. Put it first. |
| `AllOf<R...>`, `AnyOf<R...>`, `Not<R>` | combine other rules | anything the above can't say alone |
| `Satisfies<lambda, "text">` | the lambda returns true | one-offs that don't deserve a struct |

The last one is the escape hatch, and it brings back the lambda style:

```cpp
using PllDivider = Validated<int,
    Satisfies<[](auto v) { return v == 1 || v % 2 == 0; }, "1 or an even number">>;
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

## Validated types in structs

They compose like any other member. A struct of validated fields carries the
same guarantee as its parts: if you have one, every field passed.

```cpp
struct RadioConfig {
    ChannelID channel;
    Baud      baud;
    Percent   volume;
};

constexpr RadioConfig defaults{ChannelID{14}, Baud{115200}, Percent{50}};  // checked at build time

void apply_config(const RadioConfig& cfg) {   // no checks, however deep it goes
    handle_request(cfg.channel);
    configure_serial(cfg.baud);
    set_volume(cfg.volume);
}
```

Layout is identical to the same struct with raw ints: same size, same
alignment, trivially copyable. `examples/domain.hpp` asserts this.

Bytes off a socket or out of flash can't carry a proof, so a packed wire
struct should stay raw. Convert it once, in one function:

```cpp
struct __attribute__((packed)) RadioConfigWire { int16_t channel; int32_t baud; int8_t volume; };

std::optional<RadioConfig> parse_config(const RadioConfigWire& wire) {
    auto channel = ChannelID::try_from(wire.channel);
    auto baud    = Baud::try_from(wire.baud);
    auto volume  = Percent::try_from(wire.volume);
    if (!channel || !baud || !volume) return std::nullopt;
    return RadioConfig{*channel, *baud, *volume};
}
```

A rule can also span several fields. `Validated<T>` works for any `T`, so
wrap a struct and write a rule that looks at the whole thing. In this example
each field is valid on its own, but the pair must also fit inside the capture
buffer, which neither field can promise alone:

```cpp
struct IQRange { BlockOffset offset; FFTSize size; };

struct WithinCaptureBuffer {
    static constexpr bool passes(const IQRange& r) { return r.offset + r.size <= kCaptureBufferSamples; }
    static std::string requirement() { return "offset + size <= " + std::to_string(kCaptureBufferSamples); }
};

using IQBlock = Validated<IQRange, WithinCaptureBuffer>;

void process_iq_block(const IQBlock& block) {
    int64_t end = block->offset + block->size;   // -> reaches the fields
}
```

A `Validated` field inside a packed struct also packs correctly on Clang.
GCC may refuse to under-align it, because the type has a user-provided
constructor and so isn't a POD, and it will warn "ignoring packed attribute
because of unpacked non-POD field". Keeping wire structs raw sidesteps that.

## Two ways in

| | When | On failure |
|---|---|---|
| `ChannelID{v}` | The value should never be wrong: constants, config, computed values | Throws `std::invalid_argument`, e.g. `value 5000: expected <= 4096`. In a `constexpr` context, the build fails instead. |
| `ChannelID::try_from(v)` | Untrusted input: user text, network, files | Returns `std::nullopt` |

Compile-time checking costs nothing at runtime and catches mistakes before the
program even exists:

```cpp
constexpr ChannelID default_channel{14};   // fine
constexpr ChannelID oops{5000};            // error: rule_violated<vetted::AtMost<4096>>
```

## Build and run

```sh
cmake -S . -B build && cmake --build build && ./build/vetted_example
```

To watch the compiler reject a bad constant:

```sh
cmake -S . -B build -DDEMO_COMPILE_ERROR=ON && cmake --build build
```

The error message names the rule that broke:

```
error: constexpr variable 'never_compiles' must be initialized by a constant expression
note: non-constexpr function 'rule_violated<vetted::AtMost<4096>>' cannot be used in a constant expression
note: in call to 'enforce<vetted::AtMost<4096>>(5000)'
```

## Is this safer? Is it faster?

Safer, yes. You cannot pass a raw `int16_t` where a `ChannelID` is expected,
so forgetting to validate is a compile error rather than a bug that ships. The
rule lives in one line, so changing 4096 to 8192 updates every layer at once.
A function that takes `ChannelID` is declaring its assumptions in its
signature, which makes it easier to review. And the only way in is the
`explicit` constructor, so every place a raw value becomes a trusted one is
greppable.

There are limits. The guarantee is only as good as the rules, so test them.
A rule also promises exactly what it says and no more. An early version of
this example had `BlockOffset = Validated<int64_t, AtLeast<0>>` and then
computed `offset + size`. `INT64_MAX` is a perfectly valid `BlockOffset` under
that rule, and the addition overflowed. The type never claimed the sum would
fit; the function assumed it. The fix is either to make the type promise what
the function needs (`BlockOffset` is now bounded by `INT64_MAX - kMaxFFTSize`)
or to validate the pair together (`IQBlock`).

Arithmetic on a validated value produces a plain `T`. That is correct, since
`offset + size` is not a `BlockOffset`, but it means the proof doesn't
propagate through math. The type says "this number is in range" and nothing
else; it is not memory safety or thread safety. And if someone adds an
"unchecked" constructor for convenience, the whole thing quietly turns back
into a plain `int`.

Faster, a little, and only because there is less to do. Compiling the
three-layer chain both ways at `-O2` on arm64:

| | Old way, per layer | Validated, per layer |
|---|---|---|
| Hot path | Range check, branch, tail call (6 instructions) | Sign-extend, tail call (2 instructions) |
| Cold path | An exception-throwing block in every function | None |
| Argument passing | `int16_t` in a register | `int16_t` in a register |

The wrapper is free: `ChannelID` is the same size as `int16_t`, trivially
copyable and trivially destructible, so it passes in registers exactly like
the raw integer. A `constexpr ChannelID` is checked at build time and emits no
code at all. The `requirement()` strings only run when a check fails; the success
path never touches them.

A correctly predicted branch is nearly free on a modern CPU, so the gain per
call is small. It adds up in deep call chains and hot loops, not in code that
runs once. And if the old code already validated only at the boundary, there
is nothing to remove. The reason to use this is safety. The speed is a side
effect.

## Things worth knowing

- Reading out is free and only going in is guarded. A `Validated<T>` converts
  back to a plain `T` implicitly, so arithmetic and printing just work. You can
  never get a `Validated` from a `T` without running the rules, because the
  constructor is `explicit`.
- There is no default constructor and there are no setters, so once built the
  value can't be changed into something invalid.
- Rules are ordinary code. `passes` runs at compile time when it can and at
  runtime when it must, and nothing is duplicated between the two paths.
- The examples use integer types because the error messages call
  `std::to_string`. For other value types, swap that for whatever formatting
  makes sense.

## License

MIT. See [LICENSE](LICENSE).
