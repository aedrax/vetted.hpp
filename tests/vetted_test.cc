// Tests for vetted.hpp. No framework: each CHECK prints the line that fails,
// and main returns the number of failures.
//
//     cmake -S . -B build && cmake --build build && ctest --test-dir build
//
// The build uses the strict warning flags in CMakeLists.txt with -Werror, so
// a warning in the header fails the build.
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#if __has_include(<format>)
#include <format>
#endif

#include <vetted.hpp>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// Compares text and prints both sides when they differ.
void check_text(const std::optional<std::string>& got, const std::optional<std::string>& want, int line) {
    if (got == want) return;
    std::printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, line,
                got ? got->c_str() : "<nullopt>", want ? want->c_str() : "<nullopt>");
    ++failures;
}
#define CHECK_TEXT(...) check_text(__VA_ARGS__, __LINE__)

using namespace vetted;

using Quantity     = Validated<int16_t, Positive, AtMost<1000>>;
using GiftQuantity = Quantity::With<AtMost<5>>;

// A bound that the tests change between constructions.
int limit = 100;
using Limited      = Validated<int, AtMostVar<limit>>;
using SmallLimited = Limited::With<AtMost<60>>;

// The message that the throwing constructor in `make` gives, or nullopt.
std::optional<std::string> thrown(auto make) {
    try {
        static_cast<void>(make());
        return std::nullopt;
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
}

// ---------------------------------------------------------------------------
// 1. The reason for a failure, with no exception
// ---------------------------------------------------------------------------

// A passing constant gives no violation at compile time.
static_assert(!Quantity::violation(5));
static_assert(!GiftQuantity::violation(Quantity{5}));

void test_violation() {
    CHECK_TEXT(Quantity::violation(250), std::nullopt);
    CHECK_TEXT(Quantity::violation(5000), "value 5000: expected <= 1000");
    // The rules run in order and stop at the first failure.
    CHECK_TEXT(Quantity::violation(0), "value 0: expected > 0");
    CHECK_TEXT(Quantity::violation(-3), "value -3: expected > 0");
    // The same text as the exception.
    CHECK_TEXT(Quantity::violation(5000), thrown([] { return Quantity{5000}; }));
    CHECK_TEXT(Quantity::violation(0), thrown([] { return Quantity{0}; }));

    // Narrowing runs only the rules that the source did not prove.
    CHECK_TEXT(GiftQuantity::violation(Quantity{3}), std::nullopt);
    CHECK_TEXT(GiftQuantity::violation(Quantity{250}), "value 250: expected <= 5");

    limit = 100;
    const Limited fifty{50};
    limit = 10;
    // The raw value fails the new bound.
    CHECK_TEXT(Limited::violation(50), "value 50: expected <= 10");
    // The narrowing form does not run AtMostVar<limit> again. Only AtMost<60> runs.
    CHECK_TEXT(SmallLimited::violation(fifty), std::nullopt);
    CHECK_TEXT(SmallLimited::violation(Limited{7}), std::nullopt);
    limit = 100;
    CHECK_TEXT(SmallLimited::violation(Limited{70}), "value 70: expected <= 60");
}

#if defined(__cpp_lib_expected)
static_assert(Quantity::check(5).has_value());
static_assert(*Quantity::check(5) == 5);
static_assert(GiftQuantity::check(Quantity{5}).has_value());

void test_check() {
    CHECK(Quantity::check(250).has_value());
    CHECK_TEXT(Quantity::check(5000).error_or(""), "value 5000: expected <= 1000");
    CHECK(GiftQuantity::check(Quantity{4}).has_value());
    CHECK_TEXT(GiftQuantity::check(Quantity{250}).error_or(""), "value 250: expected <= 5");
}
#else
void test_check() {}
#endif

// ---------------------------------------------------------------------------
// 2. Numbers in the messages
// ---------------------------------------------------------------------------

double ceiling = 1e9;
constexpr std::array<double, 2> sizes{0.1, 2.5e6};

void test_number_text() {
    // Integers print the same with std::format and std::to_string.
    CHECK_TEXT(vetted::describe_value(5000), "value 5000");
    CHECK_TEXT(vetted::describe_value(-5), "value -5");
    CHECK_TEXT(Validated<int, Positive>::violation(-5), "value -5: expected > 0");
    CHECK_TEXT((AtMost<-20>::requirement()), "<= -20");
    CHECK_TEXT((In<1, 2, 5>::requirement()), "one of {1 2 5}");
#if defined(__cpp_lib_format)
    // Floating point uses std::format: the shortest text that reads back as
    // the same value, not the six fixed decimals of std::to_string.
    CHECK_TEXT(vetted::describe_value(1e9), "value 1e+09");
    CHECK_TEXT(vetted::describe_value(0.1), "value 0.1");
    CHECK_TEXT(vetted::describe_value(2.5e6), "value 2500000");
    CHECK_TEXT(vetted::describe_value(-0.25), "value -0.25");
    CHECK_TEXT(vetted::describe_value(0.1f), "value 0.1");
    CHECK_TEXT(AtMost<1e9>::requirement(), "<= 1e+09");
    CHECK_TEXT(AtLeast<0.1>::requirement(), ">= 0.1");
    CHECK_TEXT(GreaterThan<2.5e6>::requirement(), "> 2500000");
    CHECK_TEXT(LessThan<0.5f>::requirement(), "< 0.5");
    CHECK_TEXT((In<0.1, 2.5e6>::requirement()), "one of {0.1 2500000}");
    CHECK_TEXT((NotIn<0.1, 2.5e6>::requirement()), "not one of {0.1 2500000}");
    CHECK_TEXT(AtMostVar<ceiling>::requirement(), "<= 1e+09");
    CHECK_TEXT((InVar<sizes>::requirement()), "one of {0.1 2500000}");
    CHECK_TEXT((Validated<double, AtMost<1e9>>::violation(2e9)), "value 2e+09: expected <= 1e+09");
    CHECK_TEXT((Validated<double, AtMostVar<ceiling>>::violation(0.1 + 1e9)),
               "value 1000000000.1: expected <= 1e+09");
    CHECK_TEXT((Validated<float, AtMost<2.5f>>::violation(3.75f)), "value 3.75: expected <= 2.5");
#endif
}

// ---------------------------------------------------------------------------
// 3. Rule parameters and rule lists that code can read
// ---------------------------------------------------------------------------

namespace readable {

// The Validated aliases.
using Percent = Validated<int, Between<0, 100>>;
static_assert(std::is_same_v<Quantity::value_type, int16_t>);
static_assert(std::is_same_v<Quantity::rules, std::tuple<Positive, AtMost<1000>>>);
static_assert(std::is_same_v<GiftQuantity::rules, std::tuple<Positive, AtMost<1000>, AtMost<5>>>);
static_assert(std::is_same_v<Percent::rules, std::tuple<Between<0, 100>>>);

// Value rules.
static_assert(AtLeast<-3>::min == -3);
static_assert(AtMost<1000>::max == 1000);
static_assert(std::is_same_v<decltype(AtMost<1000>::max), const int>);
static_assert(AtMost<2.5>::max == 2.5);
static_assert(GreaterThan<0>::bound == 0);
static_assert(LessThan<256>::bound == 256);
static_assert(MultipleOf<15>::divisor == 15);
static_assert(Aligned<512>::divisor == 512);
static_assert(FitsInBits<8>::bits == 8);
static_assert(OnlyBits<0x7>::mask == 0x7);
static_assert(HasBits<0x1>::mask == 0x1);
static_assert(In<1, 2, 5>::values == std::array{1, 2, 5});
static_assert(In<0.5, 2.5>::values == std::array{0.5, 2.5});

// The aliases over AllOf and Not. They stay the same types as before.
static_assert(std::is_same_v<Between<0, 100>, AllOf<AtLeast<0>, AtMost<100>>>);
static_assert(Between<-2, 50>::min == -2 && Between<-2, 50>::max == 50);
static_assert(std::is_same_v<NotIn<0, 13>, Not<In<0, 13>>>);
static_assert(NotIn<0, 13>::values == std::array{0, 13});

// Container rules.
static_assert(SizeIs<2>::size == 2);
static_assert(SizeAtLeast<3>::min == 3);
static_assert(SizeAtMost<16>::max == 16);
static_assert(std::is_same_v<decltype(SizeAtMost<16>::max), const std::size_t>);

// Text rules.
static_assert(StartsWith<"report-">::prefix == "report-");
static_assert(EndsWith<".pdf">::suffix == ".pdf");
static_assert(Contains<"-q">::part == "-q");
static_assert(OnlyChars<"ABC">::chars == "ABC");

static_assert(PortNumber::description == "a port number 1..65535");

// The specializations keep the checks and the messages.
static_assert(Percent::try_from(100) && !Percent::try_from(101) && !Percent::try_from(-1));
static_assert(Validated<int, NotIn<0, 13>>::try_from(12) && !Validated<int, NotIn<0, 13>>::try_from(13));

void test_messages() {
    CHECK_TEXT((Between<0, 100>::requirement()), ">= 0 and <= 100");
    CHECK_TEXT((NotIn<0, 13>::requirement()), "not one of {0 13}");
    CHECK_TEXT(Percent::violation(101), "value 101: expected >= 0 and <= 100");
}

#if defined(__cpp_lib_format)
// The schema walker from docs/schema.md, word for word.
// BEGIN docs/schema.md
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
// END docs/schema.md

void test_schema() {
    CHECK_TEXT(schema<Percent>(), R"({"type": "integer", "minimum": 0, "maximum": 100})");
    CHECK_TEXT(schema<Quantity>(), R"({"type": "integer", "maximum": 1000})");
    CHECK_TEXT((schema<Validated<int32_t, In<1, 2, 5>>>()), R"({"type": "integer", "enum": [1, 2, 5]})");
    CHECK_TEXT((schema<Validated<int32_t, Positive, MultipleOf<15>, AtMost<480>>>()),
               R"({"type": "integer", "maximum": 480, "multipleOf": 15})");
    CHECK_TEXT((schema<Validated<int, Between<-2, 50>, NotIn<0, 13>>>()),
               R"({"type": "integer", "minimum": -2, "maximum": 50})");
    CHECK_TEXT(schema<Validated<uint16_t, PortNumber>>(), R"({"type": "integer", "minimum": 1, "maximum": 65535})");
    CHECK_TEXT((schema<Validated<double, Finite, GreaterThan<0.0>, AtMost<1e6>>>()),
               R"({"type": "number", "maximum": 1e+06})");
    // A Var rule has no constexpr value, so the schema has nothing from it.
    CHECK_TEXT(schema<Limited>(), R"({"type": "integer"})");
}
#else
void test_schema() {}
#endif

}  // namespace readable

// ---------------------------------------------------------------------------
// 4. The version
// ---------------------------------------------------------------------------

// CMakeLists.txt passes the version from project(). The header must agree.
static_assert(VETTED_VERSION_MAJOR == VETTED_PROJECT_VERSION_MAJOR);
static_assert(VETTED_VERSION_MINOR == VETTED_PROJECT_VERSION_MINOR);
static_assert(VETTED_VERSION_PATCH == VETTED_PROJECT_VERSION_PATCH);

}  // namespace

int main() {
    test_violation();
    test_check();
    test_number_text();
    readable::test_messages();
    readable::test_schema();
    if (failures == 0) std::printf("all tests passed (C++ %ld)\n", static_cast<long>(__cplusplus));
    return failures;
}
