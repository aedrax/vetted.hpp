# Writing and combining rules

The snippets below assume `using namespace vetted;`, as the example code does.

A rule is a struct with two static functions. `passes` returns whether a
value passes. `requirement` returns the condition for the error message.
`requirement` returns the bare condition ("a power of two", "<= 1000").
`Validated` adds "expected" when it builds the message, so combinators can
nest without repeated wording.

```cpp
struct PowerOfTwo {
    static constexpr bool passes(auto v) { return v > 0 && (v & (v - 1)) == 0; }
    static std::string requirement() { return "a power of two"; }
};
```

A rule can take parameters. Give each parameter as a `static constexpr`
member, so code can read it (see [Reading the rules of a type](#reading-the-rules-of-a-type)):

```cpp
template <auto Max>
struct AtMost {
    static constexpr auto max = Max;
    static constexpr bool passes(auto v) { return v <= Max; }
    static std::string requirement() { return "<= " + detail::to_text(Max); }
};
```

## Chaining rules

List rules. They run in order and stop at the first failure:

```cpp
using ChunkSize = Validated<int32_t, PowerOfTwo, AtMost<65536>>;
```

Or bundle them into a new rule, so the intent has a name:

```cpp
template <auto Lo, auto Hi>
using Between = AllOf<AtLeast<Lo>, AtMost<Hi>>;

using Percent = Validated<int, Between<0, 100>>;
```

`AllOf`, `AnyOf` and `Not` nest to any depth, so an odd business rule still
reads as one line:

```cpp
// a floor in a building with no floor 0 and no floor 13
using Floor  = Validated<int, Between<-2, 50>, NotIn<0, 13>>;

// a coupon: a multiple of 5 percent up to 50, or exactly 100 (free)
using Coupon = Validated<int, AnyOf<AllOf<MultipleOf<5>, AtMost<50>>, In<100>>>;
```

A nested combinator builds its message from its parts. `Coupon{7}` says
"expected a multiple of 5 and <= 50, or one of {100}". Wrap it in `Named` to
keep the check and replace the message:

```cpp
using Coupon = Validated<int, Named<AnyOf<AllOf<MultipleOf<5>, AtMost<50>>, In<100>>,
                                    "a multiple of 5 up to 50, or exactly 100">>;
```

## Refining a type

`With` adds rules to an existing type without a repeat of the rules it has:

```cpp
using Quantity     = Validated<int16_t, Positive, AtMost<1000>>;
using GiftQuantity = Quantity::With<AtMost<5>>;
// the same as Validated<int16_t, Positive, AtMost<1000>, AtMost<5>>
```

Between two `Validated` types with the same `T`, the rule lists decide the
conversion:

- Widening is implicit and free. Every `Quantity` rule is in the
  `GiftQuantity` list, so a `GiftQuantity` converts to a `Quantity` with no
  check. A function that takes a `Quantity` accepts a `GiftQuantity`.
- Narrowing is explicit, and runs only the rules the source did not prove.
  `GiftQuantity{quantity}` throws if `AtMost<5>` fails.
  `GiftQuantity::try_from(quantity)` returns `nullopt`.
  `GiftQuantity::check(quantity)` (C++23) and
  `GiftQuantity::violation(quantity)` give the message
  `value 250: expected <= 5`. None of them re-runs `Positive` or
  `AtMost<1000>`.

```cpp
void gift_wrap(GiftQuantity quantity) {
    reserve_stock(quantity);                          // takes a Quantity: implicit
}

if (auto gift = GiftQuantity::try_from(quantity)) {   // checks AtMost<5> only
    gift_wrap(*gift);
}
```

Rules are matched by type. `Between<0, 100>` and the pair `AtLeast<0>,
AtMost<100>` mean the same thing, but they are different types. So a
`Validated<int, AtLeast<0>, AtMost<100>>` does not widen to a
`Validated<int, Between<0, 100>>`. The order of rules does not affect
conversion.

## Combining types

`Both`, `Common` and `Either` make a type out of two or more types with the
same `T`. Each is named by what a value has to do:

```cpp
using Quantity      = Validated<int16_t, Positive, AtMost<1000>>;
using GiftQuantity  = Quantity::With<AtMost<5>>;
using PairQuantity  = Quantity::With<Even>;

using GiftPair      = Both<GiftQuantity, PairQuantity>;
// Validated<int16_t, Positive, AtMost<1000>, AtMost<5>, Even>

using AnyQuantity   = Common<GiftQuantity, PairQuantity>;
// Validated<int16_t, Positive, AtMost<1000>>, the same type as Quantity

using PromoQuantity = Either<GiftQuantity, PairQuantity>;
// Validated<int16_t, AnyOf<AllOf<Positive, AtMost<1000>, AtMost<5>>,
//                          AllOf<Positive, AtMost<1000>, Even>>>
```

- `Both` keeps every rule of every type, in order of first appearance, with
  duplicates removed. The result is narrower than each input, so it widens
  to each one for free. A `GiftPair` goes wherever a `GiftQuantity`, a
  `PairQuantity` or a `Quantity` goes.
- `Common` keeps only the rules every type has, in the order of the first
  type. The result is wider than each input, so each one widens to it for
  free. `Common<GiftQuantity, PairQuantity>` is `Quantity` exactly.
- `Either` makes one `AnyOf` rule from the full rule lists. A value passes
  if it passes all the rules of at least one type.

All three take any number of types, and all rules are matched by type, as
for widening.

One limit: `Either` makes a new rule type, so a `GiftQuantity` does not
widen to a `PromoQuantity` by itself, even though it should in principle.
Write `PromoQuantity{gift}` or `PromoQuantity::try_from(gift)`, which
re-runs the check. `Both` and `Common` have no such gap.

## Rules over containers and text

`Validated<T>` is not limited to numbers. The container rules work for any
`T` with a `size()` that you can iterate. The text rules work for any `T`
that converts to `std::string_view`:

```cpp
using Attachment = Validated<std::vector<uint8_t>, NonEmpty, SizeAtMost<1024>>;
using Scores     = Validated<std::vector<int16_t>, NonEmpty, Each<Between<0, 100>>>;
using Milestones = Validated<std::vector<Percent>, NonEmpty, Sorted, Unique>;
using Username   = Validated<std::string_view, SizeBetween<3, 16>,
                                               OnlyChars<"abcdefghijklmnopqrstuvwxyz0123456789_">>;
using ServerAddress = Validated<std::string_view, IpAddress>;   // IPv4 or IPv6
```

`Each<R>` takes a rule type, so the whole toolbox applies to elements. The
elements can be validated types themselves, as in `Milestones`. Each
`Percent` passed its own rules, and the list rule adds an order.

The text rules also run at compile time, so a malformed constant address or
URL fails the build:

```cpp
constexpr ServerAddress loopback{"::1"};      // fine
constexpr ServerAddress oops{"2001:db8:::1"}; // error: rule_violated<AnyOf<Ipv4Address, Ipv6Address>>
```

`Hostname`, `Ipv4Address`, `Ipv6Address`, `EmailAddress`, `Url`, `MacAddress`
and `Uuid` check the shape of the text and nothing more. They do not resolve
names and do not cover every corner of the RFCs. `EmailAddress` accepts
`local@domain` with the characters RFC 5322 allows unquoted, and a domain with
at least one dot. It rejects quoted local parts and IP-literal domains, which
almost nothing uses. `Url` accepts a scheme, a colon and at least one more
character, with no whitespace. If you need a full parser, write a rule around
one. `passes` can call anything.

For a `const char*`, put `NotNull` first, so the text rules never read
through a null pointer.

## Rules against a variable

The rules above take their bound as a constant. Each rule with a parameter
also has a `Var` form. It takes a reference to a variable, so the bound can
change while the program runs:

```cpp
inline std::atomic<int16_t> stock_on_hand{250};     // the warehouse thread updates this
inline std::vector<int32_t> shipping_options{1, 2, 5};
inline std::string          sku_prefix = "SKU-";

using InStockQuantity = Quantity::With<AtMostVar<stock_on_hand>>;
using CustomShipping  = Validated<int32_t, InVar<shipping_options>>;
using Sku             = Validated<std::string_view, StartsWithVar<sku_prefix>>;
```

The variable must have static storage duration: a namespace-scope variable
(`inline` or `extern` in a header) or a static data member. The value rules
and size rules take a number. `InVar` and `NotInVar` take a container of
allowed values. The text rules take anything that converts to
`std::string_view`. A `std::atomic` works everywhere a number does. The rule
reads it with `load()`.

Four things to know:

- **The check runs once, when the value is built.** A value built before the
  variable changed is not re-checked. The type proves "passed the bound in
  force at construction", not "passes the bound now". If the second matters,
  run `try_from` on the raw value again when the variable changes.
- **The rule reads the variable with no lock.** If another thread writes it,
  make it a `std::atomic`.
- **A rule bound to a mutable variable does not run at compile time**, because
  the answer is not known at compile time. `constexpr InStockQuantity x{1};`
  fails the build. The other rules on the same type still run at compile
  time. A `Var` rule bound to an `inline constexpr` variable runs at compile
  time as usual.
- **Refinement and widening work as for any rule.** `AtMostVar<stock_on_hand>`
  is one type wherever it is spelled, so `InStockQuantity` widens to
  `Quantity` for free.
- **A schema generator cannot read the bound.** The bound is not a
  constant, so a `Var` rule gives no `static constexpr` member. Code that
  walks the rules of a type (see [schema.md](schema.md)) finds nothing to
  read in it. If the bound must appear in a schema, write it there yourself.

To write your own, take the variable as a reference template parameter and
read it with `detail::current`, which calls `load()` on an atomic. Print the
bound with `detail::to_text`, so a floating-point bound prints as the toolbox
prints it:

```cpp
template <auto& Limit>
struct AtMostVar {
    static constexpr bool passes(auto v) { return v <= detail::current(Limit); }
    static std::string requirement() { return "<= " + detail::to_text(detail::current(Limit)); }
};
```

## Reading the rules of a type

A program that makes a JSON Schema, CLI help or docs from a type must read
the bounds as values, not as the text of `requirement()`. Each rule with a
parameter gives it as a `static constexpr` member:

| Rule | Members |
|---|---|
| `AtLeast<N>` | `min` |
| `AtMost<N>` | `max` |
| `Between<Lo, Hi>` | `min`, `max` |
| `GreaterThan<N>`, `LessThan<N>` | `bound` |
| `MultipleOf<N>`, `Aligned<N>` | `divisor` |
| `FitsInBits<N>` | `bits` (a `std::size_t`) |
| `OnlyBits<Mask>`, `HasBits<Mask>` | `mask` |
| `In<a, b, c>`, `NotIn<a, b, c>` | `values`: a `std::array`. The values must all have one type. |
| `SizeIs<N>` | `size` (a `std::size_t`) |
| `SizeAtLeast<N>` | `min` (a `std::size_t`) |
| `SizeAtMost<N>` | `max` (a `std::size_t`) |
| `StartsWith<"...">` | `prefix` (a `std::string_view`) |
| `EndsWith<"...">` | `suffix` |
| `Contains<"...">` | `part` |
| `OnlyChars<"...">` | `chars` |
| `Named<R, "text">`, `Satisfies<f, "text">` | `description` (a `std::string_view`) |

The value members have the type of the template argument:
`AtMost<1000>::max` is an `int`, and `AtMost<2.5>::max` is a `double`. The
names say what a value means, not which rule gave it. `min` is a lower bound
for `AtLeast` and a lower size for `SizeAtLeast`, so read `min` as a size only
for a rule on a container.

A `Validated<T, Rules...>` gives `value_type` (`T`) and `rules`
(`std::tuple<Rules...>`). See [schema.md](schema.md) for code that visits
the rules.

`Between` and `NotIn` are aliases, so `Between<0, 100>` is the same type as
`AllOf<AtLeast<0>, AtMost<100>>`. The header specializes
`AllOf<AtLeast<Lo>, AtMost<Hi>>` and `Not<In<...>>` to add the members. They
check and print the same as the general templates. To read a combinator such
as `SizeBetween`, match its template arguments with a partial specialization,
as the walker in [schema.md](schema.md) does for `AllOf`.

`NotIn<...>::values` holds the values that FAIL. Code that reads `values` as
the allowed set must skip `Not`. [schema.md](schema.md) shows a walker that
does.

The rules with no parameter (`Positive`, `Even`, `Hostname`, ...) and the
`Var` rules give no member. A `Var` rule has no constant to give. See
[Rules against a variable](#rules-against-a-variable).

## Rules over structs

`Validated<T>` works for any `T`, so a rule can look at several fields at
once. See [structs.md](structs.md) for `Slice`, whose rule checks that an
offset and a length fit inside a file together.

## Error messages for other types

The error message prints the value when `std::to_string` can (numbers), and
quotes it when it is text (`std::string`, `std::string_view`). For a struct,
a container or a pointer, the message says only "value". Change
`describe_value` if you want a different format.

A floating-point value prints with `std::format("{}", v)` when the standard
library defines `__cpp_lib_format`: `0.1` prints as `0.1`, and `1e9` as
`1e+09`. Otherwise it prints with `std::to_string` (`0.100000`). The same
applies to the numbers in each `requirement()`, the `Var` rules included.
Integers print the same either way. A rule of your own can call
`vetted::detail::to_text(v)` to print a number the same way. `<format>` adds
compile time to each file that includes the header.
