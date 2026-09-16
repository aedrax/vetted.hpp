// vetted.hpp - validated types for C++20, in a single header.
//
// Check a value once, where it enters the program. The type carries the proof
// everywhere else. A vetted::Validated<T, Rules...> is a T that passes every
// Rule. The only way to make one is a constructor that runs the rules, so a
// function that accepts one never re-checks.
//
//     using Quantity = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<1000>>;
//
// Contents
//   1. Validated<T, Rules...>   the wrapper
//   2. The toolbox
//        rules about the value   Positive, AtMost<N>, In<...>, PowerOfTwo, FitsIn<U>, ...
//        rules from other rules  AllOf<...>, AnyOf<...>, Not<...>, Named<R, "text">, Between<Lo, Hi>, ...
//        rules about containers  NonEmpty, SizeAtMost<N>, Each<R>, Sorted, Unique, ...
//        rules about text        StartsWith<"...">, Printable, Hostname, IpAddress, Url, Uuid, ...
//        one-off rules           Satisfies<lambda, "text">
//
// Everything lives in namespace vetted. Requires C++20.
#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <iosfwd>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace vetted {

// ============================================================================
// Validated<T, Rules...>
// ============================================================================
//
// A Validated<T, Rules...> is a T that passes every Rule. The only way to
// make one is a constructor that runs the rules. A function that receives
// one never re-checks its input.
//
// A Rule is any struct with two static functions (see the toolbox below):
//
//     static constexpr bool passes(T value);   // does the value pass?
//     static std::string   requirement();      // the condition
//
// Two ways to make one:
//   - Validated{v}           throws std::invalid_argument on failure. Use it
//                            for values that must never be wrong (constants,
//                            config). In a constexpr context it fails the
//                            *build* instead.
//   - Validated::try_from(v) returns std::nullopt on failure. Use it at the
//                            boundary, for untrusted input.
//
// A type can take more rules without a repeat of the rules it has:
//
//     using GiftQuantity = Quantity::With<AtMost<5>>;
//
// Between two Validated types with the same T, the rule lists decide the
// conversion. If every rule of the target is in the source, conversion is
// implicit and free: a GiftQuantity is accepted wherever a Quantity is. If
// not, it is explicit (Validated{other} or try_from(other)) and runs only the
// rules the source did not already prove.

// What a Rule for values of type T must provide. The check is compile-time
// only, so it costs nothing at runtime. A struct that fails it is rejected at
// the `using Quantity = Validated<...>` line, with a message that names the
// missing function. Not deep inside Validated at the first call.
template <typename Rule, typename T>
concept RuleFor = requires(T v) {
    { Rule::passes(v) } -> std::convertible_to<bool>;
    { Rule::requirement() } -> std::convertible_to<std::string>;
};

// Deliberately NOT constexpr. Calling it during constant evaluation is a
// compile error, and clang/gcc print the template argument in the diagnostic,
// so the build fails with a message like:
//     "non-constexpr function 'rule_violated<AtMost<1000>>' cannot be used
//      in a constant expression"
template <typename Rule>
void rule_violated() {}

// The error message prints the value when std::to_string can (numbers), and
// quotes it when it is text (std::string, std::string_view). Otherwise it says
// only "value" (structs, see Slice in examples/domain.hpp). A `const char*`
// is not quoted, because it may be null.
template <typename T>
std::string describe_value(const T& v) {
    if constexpr (requires { std::to_string(v); }) {
        return "value " + std::to_string(v);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view> && !std::is_pointer_v<T>) {
        return "value \"" + std::string(std::string_view(v)) + "\"";
    } else {
        return "value";
    }
}

// True if Rule is one of Others. Rules are compared as types, so Between<0, 100>
// and the pair AtLeast<0>, AtMost<100> count as different rules.
template <typename Rule, typename... Others>
constexpr bool one_of = (std::is_same_v<Rule, Others> || ...);

template <typename T, RuleFor<T>... Rules>   // every Rule must satisfy RuleFor<T>
class Validated {
    T value_;

    // Runs one rule. Fails the build at compile time, throws at runtime.
    template <typename Rule>
    static constexpr void enforce(const T& v) {
        if (Rule::passes(v)) return;
        if (std::is_constant_evaluated()) {
            rule_violated<Rule>();
        } else {
            throw std::invalid_argument(describe_value(v) + ": expected " + Rule::requirement());
        }
    }

    // Runs every rule in order. The comma fold expands to
    //     enforce<Rule1>(v), enforce<Rule2>(v), ...
    static constexpr T checked(T v) {
        (enforce<Rules>(v), ...);
        return v;
    }

    // Runs only the rules that a Validated<T, Proven...> did not already run.
    template <typename... Proven>
    static constexpr T checked_beyond(T v) {
        ([&] { if constexpr (!one_of<Rules, Proven...>) enforce<Rules>(v); }(), ...);
        return v;
    }

    struct already_checked {};
    constexpr Validated(T v, already_checked) : value_(v) {}

public:
    // Throwing constructor. `explicit`, so a raw T never becomes a Validated by
    // accident. Every conversion is a visible decision to run the checks.
    constexpr explicit Validated(T v) : value_(checked(v)) {}

    // Non-throwing constructor, for input you do not trust.
    static constexpr std::optional<Validated> try_from(T v) {
        if ((Rules::passes(v) && ...)) {
            return Validated{v, already_checked{}};
        }
        return std::nullopt;
    }

    // This type with more rules. Quantity::With<AtMost<5>> is
    // Validated<int16_t, Positive, AtMost<1000>, AtMost<5>>.
    template <RuleFor<T>... More>
    using With = Validated<T, Rules..., More...>;

    // Widening: the other type's rules include all of ours, so there is
    // nothing to check. Implicit, so the narrower type is accepted wherever
    // the wider one is.
    template <RuleFor<T>... Proven>
        requires (one_of<Rules, Proven...> && ...)
    constexpr Validated(const Validated<T, Proven...>& other) : value_(other.get()) {}

    // Narrowing: the other type lacks some of our rules. Explicit, like the
    // constructor from a raw T, and runs only the rules it lacks.
    template <RuleFor<T>... Proven>
        requires (!(one_of<Rules, Proven...> && ...))
    constexpr explicit Validated(const Validated<T, Proven...>& other)
        : value_(checked_beyond<Proven...>(other.get())) {}

    // Non-throwing narrowing. Runs only the rules the other type lacks.
    template <RuleFor<T>... Proven>
    static constexpr std::optional<Validated> try_from(const Validated<T, Proven...>& other) {
        if (((one_of<Rules, Proven...> || Rules::passes(other.get())) && ...)) {
            return Validated{other.get(), already_checked{}};
        }
        return std::nullopt;
    }

    constexpr const T& get() const { return value_; }

    // Reads back as a plain T, so arithmetic and printing work as usual.
    // Reading out is free. Only going in is guarded.
    constexpr operator T() const { return value_; }

    // For a T that is a struct, block->offset instead of block.get().offset.
    constexpr const T* operator->() const { return &value_; }

    // Prints as a plain T. The conversion above is not enough, because the
    // stream operator for std::string_view (and many other types) is a template,
    // and templates do not see through user conversions. Only <iosfwd> is
    // needed. The body is resolved at the call site, and the caller has the
    // stream header.
    template <typename C, typename Tr>
    friend std::basic_ostream<C, Tr>& operator<<(std::basic_ostream<C, Tr>& os, const Validated& v)
        requires requires { os << v.value_; }
    {
        return os << v.value_;
    }
};

// ============================================================================
// The toolbox
// ============================================================================
//
// A rule is any struct with `passes` and `requirement`.
// `requirement` returns the bare condition ("a power of two", "<= 1000").
// Validated adds "expected" when it builds the error message, so combinators
// can nest without repeated wording.
//
// There are four kinds:
//   - rules about the value       (Positive, AtMost<N>, In<a, b, c>, ...)
//     their template parameters, if any, are VALUES
//   - rules built from other rules (AllOf<...>, AnyOf<...>, Not<...>, Named<...>)
//     their template parameters are RULE TYPES
//   - rules about containers      (NonEmpty, SizeAtMost<N>, Each<R>, Sorted, ...)
//     for any T with a size that you can iterate: strings, vectors, arrays, spans
//   - rules about text            (StartsWith<"...">, IpAddress, Url, ...)
//     for any T that converts to std::string_view
// plus Satisfies<lambda, "text"> for a one-off rule that does not deserve a struct.

// fixed_string exists only so that a string literal can be a template parameter.
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    constexpr fixed_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    constexpr std::string_view view() const { return {data, N - 1}; }  // without the NUL
};

// ---------------------------------------------------------------------------
// Rules about the value
// ---------------------------------------------------------------------------

struct Positive {
    static constexpr bool passes(auto v) { return v > 0; }
    static std::string requirement() { return "> 0"; }
};

// Zero or more. Positive, NonZero and NonNegative are the three sign checks.
struct NonNegative {
    static constexpr bool passes(auto v) { return v >= 0; }
    static std::string requirement() { return ">= 0"; }
};

template <auto Min>
struct AtLeast {
    static constexpr bool passes(auto v) { return v >= Min; }
    static std::string requirement() { return ">= " + std::to_string(Min); }
};

template <auto Max>
struct AtMost {
    static constexpr bool passes(auto v) { return v <= Max; }
    static std::string requirement() { return "<= " + std::to_string(Max); }
};

struct PowerOfTwo {
    static constexpr bool passes(auto v) { return v > 0 && (v & (v - 1)) == 0; }
    static std::string requirement() { return "a power of two"; }
};

template <auto N>
struct LessThan {
    static constexpr bool passes(auto v) { return v < N; }
    static std::string requirement() { return "< " + std::to_string(N); }
};

template <auto N>
struct GreaterThan {
    static constexpr bool passes(auto v) { return v > N; }
    static std::string requirement() { return "> " + std::to_string(N); }
};

// Unlike Positive, this allows negative values. Use it for divisors and
// strides, where -1 is valid and 0 is not.
struct NonZero {
    static constexpr bool passes(auto v) { return v != 0; }
    static std::string requirement() { return "non-zero"; }
};

struct Even {
    static constexpr bool passes(auto v) { return v % 2 == 0; }
    static std::string requirement() { return "even"; }
};

struct Odd {
    static constexpr bool passes(auto v) { return v % 2 != 0; }
    static std::string requirement() { return "odd"; }
};

// For raw pointers and copyable smart pointers. Put it first, so the rules
// after it never read through a null pointer.
struct NotNull {
    static constexpr bool passes(const auto& p) { return p != nullptr; }
    static std::string requirement() { return "not null"; }
};

template <auto N>
struct MultipleOf {
    static_assert(N != 0, "MultipleOf<0> divides by zero");
    static constexpr bool passes(auto v) { return v % N == 0; }
    static std::string requirement() { return "a multiple of " + std::to_string(N); }
};

// The same check as MultipleOf, named for sizes and addresses.
template <auto N>
using Aligned = MultipleOf<N>;

// Passes if the value is representable in an N-bit unsigned field.
template <std::size_t N>
struct FitsInBits {
    static constexpr bool passes(auto v) {
        if constexpr (N >= std::numeric_limits<unsigned long long>::digits) {
            // The upper bound 2^N is wider than `unsigned long long`, so every
            // non-negative value fits. Only the sign check matters. A shift by
            // this width is undefined behavior, so this branch does not
            // instantiate it (this also silences -Wshift-count-overflow).
            return v >= 0;
        } else {
            return v >= 0 && static_cast<unsigned long long>(v) < (1ULL << N);
        }
    }
    static std::string requirement() { return "representable in " + std::to_string(N) + " bits"; }
};

// Passes if the value is representable in the integer type U, so a narrowing
// cast to U is safe. The signed counterpart of FitsInBits. FitsInBits<8> is
// 0..255, FitsIn<int8_t> is -128..127.
template <std::integral U>
struct FitsIn {
    static constexpr bool passes(std::integral auto v) { return std::in_range<U>(v); }
    static std::string requirement() {
        return std::string("representable in ") + (std::is_signed_v<U> ? "a signed " : "an unsigned ") +
               std::to_string(sizeof(U) * 8) + "-bit integer";
    }
};

// Passes if the value sets no bits outside Mask. For flag words and permission bits.
template <auto Mask>
struct OnlyBits {
    static constexpr bool passes(auto v) { return (v & ~Mask) == 0; }
    static std::string requirement() { return "within bit mask " + std::to_string(Mask); }
};

// Passes if every bit in Mask is set. The counterpart of OnlyBits. OnlyBits
// says which bits may be set. HasBits says which bits must be set.
template <auto Mask>
struct HasBits {
    static constexpr bool passes(auto v) { return (v & Mask) == Mask; }
    static std::string requirement() { return "with bit mask " + std::to_string(Mask) + " set"; }
};

// Rejects NaN and infinity. NaN compares false with everything, so Between
// alone rejects NaN with a misleading message. Put Finite first.
// (v == v fails only for NaN. v - v == 0 fails only for infinity.)
struct Finite {
    static constexpr bool passes(auto v) { return v == v && v - v == 0; }
    static std::string requirement() { return "finite"; }
};

// Passes if the value is in the given set. Template parameters are values.
template <auto... Allowed>
struct In {
    static constexpr bool passes(auto v) { return ((v == Allowed) || ...); }
    static std::string requirement() {
        std::string s = "one of {";
        ((s += std::to_string(Allowed) + " "), ...);
        s.back() = '}';
        return s;
    }
};

// ---------------------------------------------------------------------------
// Rules built from other rules
// ---------------------------------------------------------------------------

// Passes only if every listed rule passes. Template parameters are rule types.
template <typename... Rules>
struct AllOf {
    static constexpr bool passes(const auto& v) { return (Rules::passes(v) && ...); }
    static std::string requirement() {
        std::string s;
        ((s += (s.empty() ? "" : " and ") + Rules::requirement()), ...);
        return s;
    }
};

// Passes if at least one listed rule passes.
template <typename... Rules>
struct AnyOf {
    static constexpr bool passes(const auto& v) { return (Rules::passes(v) || ...); }
    static std::string requirement() {
        std::string s;
        ((s += (s.empty() ? "" : ", or ") + Rules::requirement()), ...);
        return s;
    }
};

// Passes if the wrapped rule fails.
template <typename Rule>
struct Not {
    static constexpr bool passes(const auto& v) { return !Rule::passes(v); }
    static std::string requirement() { return "not " + Rule::requirement(); }
};

// Keeps the check of Rule and replaces its message. A nested combinator can
// produce "a multiple of 5 and <= 50, or one of {100}". Wrap it in Named to
// say what it means.
template <typename Rule, fixed_string Description>
struct Named {
    static constexpr bool passes(const auto& v) { return Rule::passes(v); }
    static std::string requirement() { return Description.data; }
};

// Named combinations, so the intent reads at the use site.
template <auto Lo, auto Hi>
using Between = AllOf<AtLeast<Lo>, AtMost<Hi>>;

template <auto... Values>
using NotIn = Not<In<Values...>>;

// 1..65535. Pairs with Hostname and IpAddress below.
using PortNumber = Named<Between<1, 65535>, "a port number 1..65535">;

// ---------------------------------------------------------------------------
// Rules about containers
// ---------------------------------------------------------------------------
//
// For any T that std::size, std::begin and std::end accept: std::string,
// std::string_view, std::vector, std::array, std::span, and so on.

struct NonEmpty {
    static constexpr bool passes(const auto& c) { return !std::empty(c); }
    static std::string requirement() { return "non-empty"; }
};

template <std::size_t N>
struct SizeIs {
    static constexpr bool passes(const auto& c) { return std::size(c) == N; }
    static std::string requirement() { return "of size " + std::to_string(N); }
};

template <std::size_t N>
struct SizeAtLeast {
    static constexpr bool passes(const auto& c) { return std::size(c) >= N; }
    static std::string requirement() { return "of size >= " + std::to_string(N); }
};

template <std::size_t N>
struct SizeAtMost {
    static constexpr bool passes(const auto& c) { return std::size(c) <= N; }
    static std::string requirement() { return "of size <= " + std::to_string(N); }
};

template <std::size_t Lo, std::size_t Hi>
using SizeBetween = AllOf<SizeAtLeast<Lo>, SizeAtMost<Hi>>;

// Passes if every element passes Rule. The template parameter is a rule type,
// so the whole toolbox applies to elements. Example: Each<Between<0, 100>>.
template <typename Rule>
struct Each {
    static constexpr bool passes(const auto& c) {
        for (const auto& element : c) {
            if (!Rule::passes(element)) return false;
        }
        return true;
    }
    static std::string requirement() { return "each element " + Rule::requirement(); }
};

// Ascending, equal neighbours allowed. Add Unique for strictly ascending.
// A hand-written loop, because <algorithm> alone would be a third of the
// compile time of this header.
struct Sorted {
    static constexpr bool passes(const auto& c) {
        auto it = std::begin(c);
        if (it == std::end(c)) return true;
        for (auto prev = it++; it != std::end(c); prev = it++) {
            if (*it < *prev) return false;
        }
        return true;
    }
    static std::string requirement() { return "sorted"; }
};

// No two elements equal. Compares every pair, so it is O(n^2). That is fine
// for the lookup tables and short lists it is meant for.
struct Unique {
    static constexpr bool passes(const auto& c) {
        for (auto i = std::begin(c); i != std::end(c); ++i) {
            for (auto j = std::next(i); j != std::end(c); ++j) {
                if (*i == *j) return false;
            }
        }
        return true;
    }
    static std::string requirement() { return "without duplicates"; }
};

// ---------------------------------------------------------------------------
// Rules about text
// ---------------------------------------------------------------------------
//
// For any T that converts to std::string_view: std::string, std::string_view,
// or a `const char*` (put NotNull first for that one).
//
// Hostname, Ipv4Address, Ipv6Address, EmailAddress, Url, MacAddress and Uuid
// check the SHAPE of the text, at compile time when the value is a constant.
// They do not resolve names, connect to anything, or implement every corner
// of the RFCs. They accept a well-formed value and reject the rest, which is
// what a boundary check needs. Each rule states the exact shape it accepts.

namespace detail {

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_alnum(char c) { return is_digit(c) || is_alpha(c); }
constexpr bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// One label of a hostname: 1..63 letters, digits or hyphens, and a hyphen
// is not the first or the last character.
constexpr bool is_label(std::string_view s) {
    if (s.empty() || s.size() > 63 || s.front() == '-' || s.back() == '-') return false;
    for (char c : s) {
        if (!is_alnum(c) && c != '-') return false;
    }
    return true;
}

// Labels separated by dots, at most 253 characters in all. "localhost" passes.
constexpr bool is_hostname(std::string_view s) {
    if (s.empty() || s.size() > 253) return false;
    while (true) {
        const std::size_t dot = s.find('.');
        if (!is_label(s.substr(0, dot))) return false;
        if (dot == std::string_view::npos) return true;
        s.remove_prefix(dot + 1);
    }
}

// Four decimal octets 0..255 separated by dots. No leading zeros, so "01.2.3.4"
// fails: some parsers read a leading zero as octal.
constexpr bool is_ipv4(std::string_view s) {
    int octets = 0;
    while (true) {
        std::size_t digits = 0;
        int value = 0;
        while (digits < s.size() && digits < 3 && is_digit(s[digits])) {
            value = value * 10 + (s[digits] - '0');
            ++digits;
        }
        if (digits == 0 || value > 255 || (digits > 1 && s[0] == '0')) return false;
        ++octets;
        s.remove_prefix(digits);
        if (s.empty()) return octets == 4;
        if (s[0] != '.' || octets == 4) return false;
        s.remove_prefix(1);
    }
}

// Eight groups of 1..4 hex digits separated by colons. One "::" may stand in
// for one or more groups of zeros. The last two groups may be written as an
// IPv4 address ("::ffff:192.0.2.1"). Zone IDs ("%eth0") are not accepted.
constexpr bool is_ipv6(std::string_view s) {
    int groups = 0;
    bool compressed = false;
    if (s.starts_with("::")) {
        compressed = true;
        s.remove_prefix(2);
    } else if (s.starts_with(':')) {
        return false;
    }
    while (!s.empty()) {
        std::size_t end = s.find(':');
        if (end == std::string_view::npos) end = s.size();
        const std::string_view group = s.substr(0, end);
        if (group.find('.') != std::string_view::npos) {
            // An embedded IPv4 address must be the last thing in the text.
            if (end != s.size() || !is_ipv4(group)) return false;
            groups += 2;
            break;
        }
        if (group.empty() || group.size() > 4) return false;
        for (char c : group) {
            if (!is_hex(c)) return false;
        }
        ++groups;
        s.remove_prefix(end);
        if (s.empty()) break;
        s.remove_prefix(1);  // the ':'
        if (s.starts_with(':')) {
            if (compressed) return false;  // a second "::"
            compressed = true;
            s.remove_prefix(1);
        } else if (s.empty()) {
            return false;  // a single trailing ':'
        }
    }
    return compressed ? groups < 8 : groups == 8;
}

// The characters RFC 5322 allows in an unquoted local part, except the dot.
// is_email handles the dot itself.
constexpr bool is_atext(char c) {
    return is_alnum(c) || std::string_view{"!#$%&'*+-/=?^_`{|}~"}.find(c) != std::string_view::npos;
}

// local@domain. The local part is 1..64 atext characters with single dots
// between them. The domain is a hostname with at least one dot. This is the
// shape of almost every real address. It rejects quoted local parts,
// comments, and IP-literal domains. RFC 5322 allows them, and almost nothing
// uses them.
constexpr bool is_email(std::string_view s) {
    const std::size_t at = s.find('@');
    if (at == std::string_view::npos || at == 0 || at > 64) return false;
    const std::string_view local = s.substr(0, at);
    const std::string_view domain = s.substr(at + 1);
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string_view::npos) {
        return false;
    }
    for (char c : local) {
        if (c != '.' && !is_atext(c)) return false;
    }
    return domain.find('.') != std::string_view::npos && is_hostname(domain);
}

// scheme ":" then at least one more character, with no whitespace or control
// characters anywhere. The scheme is a letter followed by letters, digits,
// "+", "-" or "." (RFC 3986). "https://example.org/fw" and "mailto:a@b.c"
// pass. "example.org/fw" fails, because it has no scheme.
constexpr bool is_url(std::string_view s) {
    if (s.empty() || !is_alpha(s[0])) return false;
    std::size_t i = 1;
    while (i < s.size() && (is_alnum(s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')) ++i;
    if (i + 1 >= s.size() || s[i] != ':') return false;
    for (char c : s) {
        if (c <= ' ' || c == 0x7f) return false;
    }
    return true;
}

// Six pairs of hex digits, all separated by ':' or all by '-'.
constexpr bool is_mac(std::string_view s) {
    if (s.size() != 17) return false;
    const char separator = s[2];
    if (separator != ':' && separator != '-') return false;
    for (std::size_t i = 0; i < 17; ++i) {
        const bool at_separator = i % 3 == 2;
        if (at_separator ? s[i] != separator : !is_hex(s[i])) return false;
    }
    return true;
}

// 8-4-4-4-12 hex digits, so 36 characters with hyphens at 8, 13, 18 and 23.
// Any version and any case.
constexpr bool is_uuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const bool at_hyphen = i == 8 || i == 13 || i == 18 || i == 23;
        if (at_hyphen ? s[i] != '-' : !is_hex(s[i])) return false;
    }
    return true;
}

// Well-formed UTF-8 as the Unicode standard defines it (table 3-7): no
// overlong forms, no surrogates, nothing above U+10FFFF, no truncated
// sequence at the end.
template <typename It>
constexpr bool is_utf8(It it, It end) {
    auto byte = [](auto c) { return static_cast<unsigned>(static_cast<unsigned char>(c)); };
    auto in = [](unsigned b, unsigned lo, unsigned hi) { return b >= lo && b <= hi; };
    while (it != end) {
        const unsigned lead = byte(*it++);
        if (lead < 0x80) continue;
        unsigned lo = 0x80, hi = 0xBF;  // the range for the second byte
        int continuation = 0;
        if (in(lead, 0xC2, 0xDF)) {
            continuation = 1;
        } else if (in(lead, 0xE0, 0xEF)) {
            continuation = 2;
            if (lead == 0xE0) lo = 0xA0;
            if (lead == 0xED) hi = 0x9F;
        } else if (in(lead, 0xF0, 0xF4)) {
            continuation = 3;
            if (lead == 0xF0) lo = 0x90;
            if (lead == 0xF4) hi = 0x8F;
        } else {
            return false;
        }
        for (int i = 0; i < continuation; ++i) {
            if (it == end || !in(byte(*it++), lo, hi)) return false;
            lo = 0x80;
            hi = 0xBF;
        }
    }
    return true;
}

}  // namespace detail

// Every character is 0x20..0x7E: no control characters, tabs or newlines,
// and nothing outside ASCII. Text that is safe to log or show on a display.
struct Printable {
    static constexpr bool passes(std::string_view s) {
        for (char c : s) {
            if (c < 0x20 || c > 0x7E) return false;
        }
        return true;
    }
    static std::string requirement() { return "printable ASCII"; }
};

template <fixed_string Prefix>
struct StartsWith {
    static constexpr bool passes(std::string_view s) { return s.starts_with(Prefix.view()); }
    static std::string requirement() { return "starting with \"" + std::string(Prefix.view()) + "\""; }
};

template <fixed_string Suffix>
struct EndsWith {
    static constexpr bool passes(std::string_view s) { return s.ends_with(Suffix.view()); }
    static std::string requirement() { return "ending with \"" + std::string(Suffix.view()) + "\""; }
};

template <fixed_string Part>
struct Contains {
    static constexpr bool passes(std::string_view s) {
        return s.find(Part.view()) != std::string_view::npos;
    }
    static std::string requirement() { return "containing \"" + std::string(Part.view()) + "\""; }
};

// Passes if every character is in Allowed. Combine with NonEmpty if the
// empty string must fail.
template <fixed_string Allowed>
struct OnlyChars {
    static constexpr bool passes(std::string_view s) {
        return s.find_first_not_of(Allowed.view()) == std::string_view::npos;
    }
    static std::string requirement() {
        return "only characters from \"" + std::string(Allowed.view()) + "\"";
    }
};

struct Hostname {
    static constexpr bool passes(std::string_view s) { return detail::is_hostname(s); }
    static std::string requirement() { return "a hostname"; }
};

struct Ipv4Address {
    static constexpr bool passes(std::string_view s) { return detail::is_ipv4(s); }
    static std::string requirement() { return "an IPv4 address"; }
};

struct Ipv6Address {
    static constexpr bool passes(std::string_view s) { return detail::is_ipv6(s); }
    static std::string requirement() { return "an IPv6 address"; }
};

using IpAddress = AnyOf<Ipv4Address, Ipv6Address>;

struct EmailAddress {
    static constexpr bool passes(std::string_view s) { return detail::is_email(s); }
    static std::string requirement() { return "an email address"; }
};

struct Url {
    static constexpr bool passes(std::string_view s) { return detail::is_url(s); }
    static std::string requirement() { return "a URL"; }
};

struct MacAddress {
    static constexpr bool passes(std::string_view s) { return detail::is_mac(s); }
    static std::string requirement() { return "a MAC address"; }
};

struct Uuid {
    static constexpr bool passes(std::string_view s) { return detail::is_uuid(s); }
    static std::string requirement() { return "a UUID"; }
};

// Unlike the other text rules, this one takes any container of bytes. So it
// also works on a std::vector<uint8_t> or std::span<const std::byte> straight
// off the wire.
struct Utf8 {
    static constexpr bool passes(const auto& bytes) { return detail::is_utf8(std::begin(bytes), std::end(bytes)); }
    static std::string requirement() { return "well-formed UTF-8"; }
};

// ---------------------------------------------------------------------------
// One-off rules
// ---------------------------------------------------------------------------

// Satisfies makes a one-off rule from a lambda, with no struct to write.
template <auto Predicate, fixed_string Description>
struct Satisfies {
    static constexpr bool passes(const auto& v) { return Predicate(v); }
    static std::string requirement() { return Description.data; }
};

}  // namespace vetted
