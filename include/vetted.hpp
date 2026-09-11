// vetted.hpp - validated types for C++20, in a single header.
//
// Check a value once, where it enters the program, and let the type carry the
// proof everywhere else. A vetted::Validated<T, Rules...> is a T that is known
// to satisfy every Rule; the only way to make one is through a constructor
// that runs the rules, so functions that accept one never re-check.
//
//     using ChannelID = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<4096>>;
//
// Contents
//   1. Validated<T, Rules...>   the wrapper
//   2. The toolbox              Positive, AtMost<N>, In<...>, AllOf<...>, Satisfies<...>, ...
//
// Everything lives in namespace vetted. Requires C++20.
#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace vetted {

// ============================================================================
// Validated<T, Rules...>
// ============================================================================
//
// A Validated<T, Rules...> is a T that is known to satisfy every Rule.
// The only way to obtain one is through a constructor that runs the rules,
// so if a function receives one, the checks already happened. Functions that
// accept a Validated never need to re-check their input.
//
// A Rule is any struct with two static functions (see the toolbox below):
//
//     static constexpr bool passes(T value);   // does the value pass?
//     static std::string   requirement();      // the condition
//
// Two ways to get one:
//   - Validated{v}           throws std::invalid_argument on failure. Use it
//                            for values that should never be wrong (constants,
//                            config). In a constexpr context it fails the
//                            *build* instead.
//   - Validated::try_from(v) returns std::nullopt on failure. Use it at the
//                            boundary, for untrusted input.

// What it takes to be a Rule for values of type T. This is a compile-time
// contract only so it costs nothing at runtime, and a struct that doesn't meet
// it is rejected at the `using ChannelID = Validated<...>` line with a message
// that says which requirement is missing, instead of somewhere deep inside
// Validated when the rule is first called.
template <typename Rule, typename T>
concept RuleFor = requires(T v) {
    { Rule::passes(v) } -> std::convertible_to<bool>;
    { Rule::requirement() } -> std::convertible_to<std::string>;
};

// Deliberately NOT constexpr. Calling it during constant evaluation is a
// compile error, and clang/gcc print the template argument in the diagnostic,
// so the build fails with a message like:
//     "non-constexpr function 'rule_violated<AtMost<4096>>' cannot be used
//      in a constant expression"
template <typename Rule>
void rule_violated() {}

// Error messages print the value when std::to_string can (numbers) and just
// say "value" otherwise (structs, see IQBlock in examples/domain.hpp).
template <typename T>
std::string describe_value(const T& v) {
    if constexpr (requires { std::to_string(v); }) {
        return "value " + std::to_string(v);
    } else {
        return "value";
    }
}

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

    struct already_checked {};
    constexpr Validated(T v, already_checked) : value_(v) {}

public:
    // Throwing constructor. `explicit` so a raw T never silently becomes Validated
    // every conversion is a visible decision to run the checks.
    constexpr explicit Validated(T v) : value_(checked(v)) {}

    // Non-throwing constructor for input you don't trust.
    static constexpr std::optional<Validated> try_from(T v) {
        if ((Rules::passes(v) && ...)) {
            return Validated{v, already_checked{}};
        }
        return std::nullopt;
    }

    constexpr const T& get() const { return value_; }

    // Reads back as a plain T, so arithmetic and printing just work.
    // Going out of the type is free, only going in is guarded.
    constexpr operator T() const { return value_; }

    // For a T that is a struct, block->offset instead of block.get().offset.
    constexpr const T* operator->() const { return &value_; }
};

// ============================================================================
// The toolbox
// ============================================================================
//
// A rule is any struct with `passes` and `requirement`. 
// `requirement` returns the bare condition ("a power of two", "<= 4096")
// Validated prefixes it with "expected" when it builds the error message,
// so combinators can nest without the wording piling up.
//
// There are two kinds here:
//   - rules about the value       (Positive, AtMost<N>, In<a, b, c>, ...)
//     their template parameters, if any, are VALUES
//   - rules built from other rules (AllOf<...>, AnyOf<...>, Not<...>)
//     their template parameters are RULE TYPES
// plus Satisfies<lambda, "text"> for one-off rules that don't deserve a struct.

// ---------------------------------------------------------------------------
// Rules about the value
// ---------------------------------------------------------------------------

struct Positive {
    static constexpr bool passes(auto v) { return v > 0; }
    static std::string requirement() { return "> 0"; }
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
// strides, where -1 is fine and 0 is not.
struct NonZero {
    static constexpr bool passes(auto v) { return v != 0; }
    static std::string requirement() { return "non-zero"; }
};

template <auto N>
struct MultipleOf {
    static constexpr bool passes(auto v) { return v % N == 0; }
    static std::string requirement() { return "a multiple of " + std::to_string(N); }
};

// Same check as MultipleOf, just a different intent. That being sizes and addresses.
template <auto N>
using Aligned = MultipleOf<N>;

// Passes if the value is representable in an N-bit unsigned field.
template <std::size_t N>
struct FitsInBits {
    static constexpr bool passes(auto v) {
        return v >= 0 && static_cast<unsigned long long>(v) < (1ULL << N);
    }
    static std::string requirement() { return "representable in " + std::to_string(N) + " bits"; }
};

// Passes if the value sets no bits outside Mask. For flag words and register writes.
template <auto Mask>
struct OnlyBits {
    static constexpr bool passes(auto v) { return (v & ~Mask) == 0; }
    static std::string requirement() { return "within bit mask " + std::to_string(Mask); }
};

// Rejects NaN and infinity. NaN compares false with everything, so Between
// on its own would reject NaN with a misleading message. Put Finite first.
// (v == v fails only for NaN; v - v == 0 fails only for infinity.)
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
    static constexpr bool passes(auto v) { return (Rules::passes(v) && ...); }
    static std::string requirement() {
        std::string s;
        ((s += (s.empty() ? "" : " and ") + Rules::requirement()), ...);
        return s;
    }
};

// Passes if at least one listed rule passes.
template <typename... Rules>
struct AnyOf {
    static constexpr bool passes(auto v) { return (Rules::passes(v) || ...); }
    static std::string requirement() {
        std::string s;
        ((s += (s.empty() ? "" : ", or ") + Rules::requirement()), ...);
        return s;
    }
};

// Passes if the wrapped rule fails.
template <typename Rule>
struct Not {
    static constexpr bool passes(auto v) { return !Rule::passes(v); }
    static std::string requirement() { return "not " + Rule::requirement(); }
};

// Named combinations, so the intent reads at the use site.
template <auto Lo, auto Hi>
using Between = AllOf<AtLeast<Lo>, AtMost<Hi>>;

template <auto... Values>
using NotIn = Not<In<Values...>>;

// fixed_string exists only so a string literal can be a template parameter.
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    constexpr fixed_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
};

// Satisfies is just for a one-off rule from a lambda, so no struct needed
template <auto Predicate, fixed_string Description>
struct Satisfies {
    static constexpr bool passes(auto v) { return Predicate(v); }
    static std::string requirement() { return Description.data; }
};

}  // namespace vetted
