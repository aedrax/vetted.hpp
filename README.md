<p align="center">
  <img src="./assets/readme/hero.svg" width="100%" alt="vetted.hpp: a single-header C++20 library for validated types. Check a value once, where it enters; the type carries the proof after that. A raw value that passes becomes a Quantity and flows through three functions with no re-check; one that fails is rejected at the boundary.">
</p>

A `vetted::Validated<T, Rules...>` is a `T` that passes every rule. The only
way to make one is a constructor that runs the rules, so a function that
receives one can use the value without a check. You validate once, where
untrusted data enters. The type carries the proof through every layer below.

## The problem it removes

Say `handle_request()` calls `add_to_cart()`, which calls `reserve_stock()`,
and all three take an item quantity. If that number is a plain `int16_t`,
none of them knows whether the caller checked it. So they all check:

```cpp
void reserve_stock(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    ...
}
void add_to_cart(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    reserve_stock(quantity);
}
void handle_request(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    add_to_cart(quantity);
}
```

That is three copies of one rule. A fourth layer means a fourth copy. Change
the rule and you must find them all.

Instead, make "a checked quantity" its own type:

```cpp
using Quantity = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<1000>>;

void reserve_stock(Quantity quantity)  { ... }
void add_to_cart(Quantity quantity)    { reserve_stock(quantity); }
void handle_request(Quantity quantity) { add_to_cart(quantity); }
```

No checks anywhere. Validation is visible in one place: where untrusted data
becomes a `Quantity`.

```cpp
std::optional<Quantity> parse_quantity(std::string_view text) {
    int16_t raw = /* parse the digits */;
    return Quantity::try_from(raw);   // nullopt if any rule fails
}
```

When the value is a constant, the compiler runs the rules at build time and
names the rule that failed:

```
constexpr Quantity oops{5000};
// error: constexpr variable 'oops' must be initialized by a constant expression
// note: non-constexpr function 'rule_violated<vetted::AtMost<1000>>' cannot be used in a constant expression
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
  <img src="./assets/readme/anatomy.svg" width="100%" alt="Anatomy of the declaration Validated of int16_t, Positive, AtMost 1000: int16_t is the value type and reads back as a plain int16_t; Positive and AtMost are rules that run in order, and each rule is a struct with a passes function and a requirement function.">
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
| `Quantity{v}` | The value must never be wrong: constants, config, computed values | Throws `std::invalid_argument`, for example `value 5000: expected <= 1000`. In a `constexpr` context, the build fails instead. |
| `Quantity::try_from(v)` | Untrusted input: user text, network, files | Returns `std::nullopt` |

## The toolbox

`vetted.hpp` ships with these rules. Most are three lines, so add your own.

| Rule | Passes when | Typical use |
|---|---|---|
| `Positive` | `v > 0` | counts, sizes |
| `NonNegative` | `v >= 0` | offsets, delays |
| `NonZero` | `v != 0` | divisors, steps (negative allowed) |
| `Even`, `Odd` | `v % 2 == 0`, `v % 2 != 0` | image sizes, vote panels |
| `AtLeast<N>`, `AtMost<N>` | `v >= N`, `v <= N` | closed ranges |
| `GreaterThan<N>`, `LessThan<N>` | `v > N`, `v < N` | open ranges, `[0, size)` |
| `Between<Lo, Hi>` | both bounds inclusive | percentages, quantities |
| `In<a, b, c>` | `v` equals one of them | shipping options, enum-like ints |
| `NotIn<a, b, c>` | `v` equals none of them | skipped floors, reserved ports |
| `PowerOfTwo` | one bit set | buffer sizes, hash tables |
| `MultipleOf<N>`, `Aligned<N>` | `v % N == 0` | time slots, disk offsets |
| `FitsInBits<N>` | `0 <= v < 2^N` | color channels, small fields |
| `FitsIn<U>` | `v` is representable in integer type `U` | a safe narrowing cast, `FitsIn<int8_t>` |
| `OnlyBits<Mask>` | no bits set outside `Mask` | permission bits |
| `HasBits<Mask>` | every bit in `Mask` set | a required read bit |
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
| `Each<R>` | every element passes `R`, for example `Each<Between<0, 100>>` |
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
using WebhookUrl = Validated<std::string_view, Url, StartsWith<"https://">>;
constexpr WebhookUrl default_hook{"https://example.org/hooks"};   // the compiler checks the shape
```

`Satisfies` is the escape hatch. It makes a rule from a lambda:

```cpp
using PlayerCount = Validated<int,
    Satisfies<[](auto v) { return v == 1 || v % 2 == 0; }, "1 or an even number">>;
```

### Chaining rules

List rules. They run in order and stop at the first failure:

```cpp
using ChunkSize = Validated<int32_t, PowerOfTwo, AtMost<65536>>;
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
using Quantity     = Validated<int16_t, Positive, AtMost<1000>>;
using GiftQuantity = Quantity::With<AtMost<5>>;   // gift wrap for at most 5 items
```

A `GiftQuantity` is accepted wherever a `Quantity` is, with no conversion
written and no re-check, because its rules include every `Quantity` rule.
The other direction is explicit, like construction from a raw value, and runs
only the rule a `Quantity` did not already prove:

```cpp
void gift_wrap(GiftQuantity quantity) { reserve_stock(quantity); }   // takes a Quantity: fine

if (auto gift = GiftQuantity::try_from(quantity)) gift_wrap(*gift);  // checks AtMost<5> only
```

Rules are matched by type, so `Between<0, 100>` and the pair `AtLeast<0>,
AtMost<100>` are different rules. The order of rules does not affect
conversion.

## Limits

- The guarantee is only as good as the rules. Test them.
- A rule promises exactly what it says and no more. If a function needs
  `offset + length` to fit, the type must promise that, not only `>= 0`.
- Arithmetic on a validated value gives a plain `T`, so the proof does not
  survive math. Reading out is free. Only going in is guarded.
- It says "this number is in range" and nothing else. It is not memory safety
  or thread safety.

## The example

`examples/` holds a small online-shop program that uses everything above:
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
| `examples/domain.hpp` | The example's types: `Quantity`, `ChunkSize`, `ShippingDays`, ... one line each, plus structs of them |
| `examples/main.cc` | The call chain before and after, the parsing boundary, and the demo |
| `docs/` | Rules in depth, validated types in structs, and the safety and performance notes |

## FAQ

### Why not write a class with an explicit constructor by hand?

You can. This library generates that class for you. The difference is the
cost per type and how types relate. A hand-written type is 10 to 15 lines.
Here it is one `using` line. A check like `AtMost<1000>` is a value you can
reuse in any type, apply to every element with `Each`, and combine with
`AllOf`, `AnyOf` and `Not`. Refinement is decided by comparing rule lists,
not by a declared parent class, so `Quantity::With<A, B>` converts to both
`Quantity::With<A>` and `Quantity::With<B>` with no extra code. Narrowing
runs only the rules the source type did not prove. The build-time failure
and the error message are written once, in the library, not once per class.

### Can I change a validated value after I make it?

Only by assignment from another value of the same type. There are no
setters, and `get()` returns a const reference. `q = Quantity{5}` compiles
and runs the rules. `q = 5` and `q = q + 1` do not compile, because a plain
`int` cannot become a `Quantity` without a visible constructor call.

### Why does `Quantity q = 5;` not compile?

The constructor is `explicit`. Copy-initialization from a raw value would
hide the check. Write `Quantity q{5};` or `Quantity q = Quantity{5};`. Both
run the rules. A `constexpr` version runs them at build time.

### Does it work with strings, vectors, structs and pointers?

Yes. `Validated<T>` accepts any `T`. The container rules cover anything with
a `size()` that you can iterate. The text rules cover anything that converts
to `std::string_view`. For a struct, write a rule that reads the whole
struct. For a pointer, put `NotNull` first. The example has one of each.

### Does it copy or move the value in?

The constructor takes `T` by value, so a moved argument is moved in.
`Comment c{std::move(text)}` leaves `text` empty.

### Is there a default constructor?

No. A default value would have to pass the rules, and the library cannot
know one. `std::vector<Quantity>` works, because `push_back` and
`emplace_back` do not need one. A plain array `Quantity arr[3]` does not
compile. For "not set yet", use `std::optional<Quantity>`.

### Does it work as a map key, or with `std::sort`?

For numeric `T`, yes. `std::sort`, `std::map` and `==` work through the
implicit conversion to `T`. `std::unordered_map` does not, because there is
no `std::hash` specialization. Hash `q.get()` instead, or add one yourself.

### Can I build with exceptions disabled?

Yes, if you use only `try_from`. The throwing constructor contains a `throw`,
and the compiler rejects it under `-fno-exceptions` only when you use it.
`try_from` never throws.

### Does it need C++20?

Yes. It uses concepts, `auto` parameters, string literals as template
parameters, and `std::is_constant_evaluated`. There is no C++17 build.

### How do I write my own rule?

A struct with a `static constexpr bool passes(auto v)` and a
`static std::string requirement()`. That is all. See
[How it works](#how-it-works) and [docs/rules.md](docs/rules.md).

### What if two rules contradict each other?

The type compiles, and nothing passes. `Validated<int, Positive, AtMost<0>>`
rejects every value. The library does not check that a rule list is
satisfiable, so test each type with at least one value that passes.

### What does it cost at runtime?

Nothing on the read side. The wrapper is the same size as `T` and passes in
registers. The rules run once, on the way in. See
[docs/safety-and-performance.md](docs/safety-and-performance.md).

## License

MIT. See [LICENSE](LICENSE).
