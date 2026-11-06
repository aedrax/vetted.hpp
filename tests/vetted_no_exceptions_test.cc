// Built with -fno-exceptions. The non-throwing functions must compile and
// work without exceptions: try_from, violation and (C++23) check.
#include <cstdint>
#include <cstdio>

#include <vetted.hpp>

using Quantity = vetted::Validated<int16_t, vetted::Positive, vetted::AtMost<1000>>;

int main() {
    int failures = 0;
    if (!Quantity::try_from(250)) ++failures;
    if (Quantity::violation(5000) != "value 5000: expected <= 1000") ++failures;
#if defined(__cpp_lib_expected)
    if (!Quantity::check(250)) ++failures;
#endif
    std::printf(failures == 0 ? "no-exceptions test passed\n" : "no-exceptions test FAILED\n");
    return failures;
}
