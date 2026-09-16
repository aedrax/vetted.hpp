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

A rule can take parameters:

```cpp
template <auto Max>
struct AtMost {
    static constexpr bool passes(auto v) { return v <= Max; }
    static std::string requirement() { return "<= " + std::to_string(Max); }
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
  `GiftQuantity::try_from(quantity)` returns `nullopt`. Neither one re-runs
  `Positive` or `AtMost<1000>`.

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

## Rules over structs

`Validated<T>` works for any `T`, so a rule can look at several fields at
once. See [structs.md](structs.md) for `Slice`, whose rule checks that an
offset and a length fit inside a file together.

## Error messages for other types

The error message prints the value when `std::to_string` can (numbers), and
quotes it when it is text (`std::string`, `std::string_view`). For a struct,
a container or a pointer, the message says only "value". Change
`describe_value` if you want a different format.
