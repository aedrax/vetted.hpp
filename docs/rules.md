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

## Rules over structs

`Validated<T>` works for any `T`, so a rule can look at several fields at
once. See [structs.md](structs.md) for `IQBlock`, whose rule checks that an
offset and a size fit inside a buffer together.

## Error messages for non-integer types

The bundled rules and the example use integer and floating-point types
because the error messages call `std::to_string`. For a struct `T`, the
message says just "value" instead. For other value types, swap that for
whatever formatting makes sense.
