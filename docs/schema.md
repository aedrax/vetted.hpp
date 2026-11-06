# A schema from the rules of a type

This example walks the rules of a `Validated` type and prints a small JSON
Schema: `minimum`, `maximum`, `multipleOf` and `enum`. It shows how to read
the rules. It is not part of the library. Copy it and change it for your
format: JSON Schema, CLI help, or docs.

The walker uses the members that
[rules.md](rules.md#reading-the-rules-of-a-type) lists: `value_type` and
`rules` on the type, and `min`, `max`, `divisor` and `values` on the rules.
`tests/vetted_test.cc` holds the same code and checks its output.

```cpp
#include <format>
#include <string>
#include <tuple>
#include <type_traits>

#include <vetted.hpp>

using namespace vetted;

// The parts of a JSON Schema that this example fills. Empty if no rule sets it.
struct Fields {
    std::string minimum, maximum, multiple_of, enumeration;
};

// Any rule: read the values it has.
template <typename Rule>
struct Walk {
    static void add(Fields& f) {
        if constexpr (requires { Rule::min; }) f.minimum = std::format("{}", Rule::min);
        if constexpr (requires { Rule::max; }) f.maximum = std::format("{}", Rule::max);
        if constexpr (requires { Rule::divisor; }) f.multiple_of = std::format("{}", Rule::divisor);
        if constexpr (requires { Rule::values; }) {
            for (auto v : Rule::values) {
                f.enumeration += std::format("{}{}", f.enumeration.empty() ? "[" : ", ", v);
            }
            f.enumeration += "]";
        }
    }
};

// AllOf: every rule in it applies. Between<Lo, Hi> is AllOf<AtLeast<Lo>, AtMost<Hi>>.
template <typename... Rules>
struct Walk<AllOf<Rules...>> {
    static void add(Fields& f) { (Walk<Rules>::add(f), ...); }
};

// Named: the rule in it applies. PortNumber is Named<Between<1, 65535>, "...">.
template <typename Rule, auto Description>
struct Walk<Named<Rule, Description>> {
    static void add(Fields& f) { Walk<Rule>::add(f); }
};

// Not does not fit these fields, so skip it. Without this, NotIn<0, 13>::values
// would read as an enum.
template <typename Rule>
struct Walk<Not<Rule>> {
    static void add(Fields&) {}
};

// A JSON Schema for a Validated type with a numeric T.
template <typename V>
std::string schema() {
    using T = typename V::value_type;
    static_assert(std::is_arithmetic_v<T>, "this example reads min and max as numbers");
    Fields f;
    [&]<typename... Rules>(std::type_identity<std::tuple<Rules...>>) {
        (Walk<Rules>::add(f), ...);
    }(std::type_identity<typename V::rules>{});

    std::string out = std::format(R"({{"type": "{}")", std::is_integral_v<T> ? "integer" : "number");
    if (!f.minimum.empty()) out += R"(, "minimum": )" + f.minimum;
    if (!f.maximum.empty()) out += R"(, "maximum": )" + f.maximum;
    if (!f.multiple_of.empty()) out += R"(, "multipleOf": )" + f.multiple_of;
    if (!f.enumeration.empty()) out += R"(, "enum": )" + f.enumeration;
    return out + "}";
}
```

Use it:

```cpp
using Percent      = Validated<int, Between<0, 100>>;
using ShippingDays = Validated<int32_t, In<1, 2, 5>>;
using SlotMinutes  = Validated<int32_t, Positive, MultipleOf<15>, AtMost<480>>;
using Floor        = Validated<int, Between<-2, 50>, NotIn<0, 13>>;
using ListenPort   = Validated<uint16_t, PortNumber>;

schema<Percent>();       // {"type": "integer", "minimum": 0, "maximum": 100}
schema<ShippingDays>();  // {"type": "integer", "enum": [1, 2, 5]}
schema<SlotMinutes>();   // {"type": "integer", "maximum": 480, "multipleOf": 15}
schema<Floor>();         // {"type": "integer", "minimum": -2, "maximum": 50}
schema<ListenPort>();    // {"type": "integer", "minimum": 1, "maximum": 65535}
```

Things to know before you extend it:

- **It reads members by name.** A rule of your own that gives `min` or `max`
  goes into the schema with no change to the walker.
- **It skips `Not`.** A negation does not fit these fields.
  `NotIn<0, 13>::values` holds the values that fail, so a walker that reads
  every `values` as an enum gives a wrong schema. `Floor` above shows the
  result: the bounds, and no enum.
- **It handles a numeric `T` only.** For a container or text, `min` and `max`
  come from `SizeAtLeast` and `SizeAtMost`. They are `minLength` and
  `maxLength` for a string, or `minItems` and `maxItems` for an array.
- **It cannot read a `Var` rule.** `AtMostVar<stock_on_hand>` has no
  constant, so it gives no member, and the schema has no bound from it.
- **Rules with no parameter give nothing.** `Positive` is not
  `"exclusiveMinimum": 0` unless you add a specialization of `Walk` for it.
  `GreaterThan<N>` and `LessThan<N>` both give `bound`. Specialize `Walk` for
  each one to write `exclusiveMinimum` or `exclusiveMaximum`.
