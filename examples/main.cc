#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "domain.hpp"

// ============================================================================
// The call chain validating inputs
// ============================================================================

namespace the_old_way {
// In this example, data passes through three layers of functions resulting in three 
// copies of the same check. None of them can trust the caller, so every one of them
// is defensive. Kept here only for contrast.

// Actually needs the value in range, so it checks. Fair enough.
void write_register(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    // ... stuff ...
    std::cout << "  register <- " << channel << "\n";
}

// Only forwards, but can't trust its caller, so it checks again.
void tune_radio(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    // ... stuff ...
    write_register(channel);
}

// Top of the chain where you're validating input for the first time
void handle_request(int16_t channel) {
    if (channel < 1 || channel > 4096) throw std::invalid_argument("bad channel");
    // ... stuff ...
    tune_radio(channel);
}
}  // namespace the_old_way

// The same three layers with ChannelID which is a `Validated` type. Don't need to check,
// because a ChannelID cannot exist unless the checks already passed. Each function just
// forwards the value and gets on with its job.

void write_register(ChannelID channel) {
    // ... stuff ...
    std::cout << "  register <- " << channel << "\n";
}

void tune_radio(ChannelID channel) {
    // ... stuff ...
    std::cout << "  tuning to channel " << channel << "\n";
    write_register(channel);
}

void handle_request(ChannelID channel) {
    // ... stuff ...
    std::cout << "  request for channel " << channel << "\n";
    tune_radio(channel);
}

// A few more functions that take validated types and can do math freely.

// Arithmetic on validated values, BlockOffset is bounded so the sum can't overflow.
void process_iq_block(BlockOffset offset, FFTSize size) {
    int64_t end = offset + size;  // cannot overflow: BlockOffset is bounded for exactly this
    std::cout << "  processing block [" << offset << ", " << end << ")\n";
}

// A validated struct, one rule spans both fields, so the block is known to fit.
void process_iq_block(const IQBlock& block) {
    int64_t end = block->offset + block->size;
    std::cout << "  processing block [" << block->offset << ", " << end << ") in buffer\n";
}

// A fixed set of allowed values (In<9600, 19200, ...>).
void configure_serial(Baud rate) {
    std::cout << "  serial port at " << rate << " bps\n";
}

// A closed range (Between<0, 100>).
void set_volume(Percent level) {
    std::cout << "  volume at " << level << "%\n";
}

// A range with holes (Between<0, 31> plus NotIn<0, 1, 14, 15>).
void toggle_pin(GpioPin pin) {
    std::cout << "  toggling GPIO " << pin << "\n";
}

// Nested combinators (AnyOf<AllOf<PowerOfTwo, AtMost<64>>, In<1000>>).
void start_dma(BurstLength burst) {
    std::cout << "  DMA burst of " << burst << "\n";
}

// A struct of validated fields, holding one means every field passed, so no checks.
void apply_config(const RadioConfig& cfg) {
    handle_request(cfg.channel);
    configure_serial(cfg.baud);
    set_volume(cfg.volume);
}

// ============================================================================
// Boundary where inputs are checked and validation is visible
// ============================================================================

// Untrusted text comes in, a ChannelID (or nothing) goes out. Everything
// downstream of this function gets a value it can trust.

std::optional<ChannelID> parse_channel(std::string_view text) {
    int16_t raw{};
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), raw);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;  // not a number at all
    }
    return ChannelID::try_from(raw);  // a number, but does it pass the rules?
}

// The same idea for a struct. A packed wire format holds raw integers, because
// bytes off a socket or out of flash can't carry a proof. The one conversion
// function turns it into a RadioConfig, or nothing. Downstream code never sees
// the raw version.
struct __attribute__((packed)) RadioConfigWire {
    int16_t channel;
    int32_t baud;
    int8_t  volume;
};
static_assert(sizeof(RadioConfigWire) == 7);

std::optional<RadioConfig> parse_config(const RadioConfigWire& wire) {
    auto channel = ChannelID::try_from(wire.channel);
    auto baud    = Baud::try_from(wire.baud);
    auto volume  = Percent::try_from(wire.volume);
    if (!channel || !baud || !volume) return std::nullopt;
    return RadioConfig{*channel, *baud, *volume};
}

// ============================================================================
// Demo
// ============================================================================

int main() {
    std::cout << "== Compile time: the compiler proves the constant is valid ==\n";
    std::cout << "== a ChannelID is `Validated` to be positive and at most 4096 ==\n";
    constexpr ChannelID default_channel{14};  // checked during the build
    static_assert(default_channel == 14);
    handle_request(default_channel);

#ifdef DEMO_COMPILE_ERROR
    // Build with -DDEMO_COMPILE_ERROR=ON to see this line fail to compile.
    // The diagnostic names the broken rule: rule_violated<AtMost<4096>>.
    constexpr ChannelID never_compiles{5000};
#endif

    std::cout << "\n== Runtime: untrusted input is checked once, at the boundary ==\n";
    std::cout << "== the inputs being initially validated for ChannelID types or not ==\n";
    for (std::string_view input : {"2048", "5000", "0", "abc"}) {
        std::cout << "input \"" << input << "\":\n";
        if (auto channel = parse_channel(input)) {
            handle_request(*channel);  // three layers deep, zero re-checks
        } else {
            std::cout << "  rejected\n";
        }
    }

    std::cout << "\n== The throwing constructor, for values that must never be wrong ==\n";
    std::cout << "== These could have been compile-time checked, but for the purpose of the demo they aren't ==\n";
    try {
        FFTSize fft{1000};
        process_iq_block(BlockOffset{0}, fft);
    } catch (const std::invalid_argument& e) {
        std::cout << "  FFTSize{1000} threw: " << e.what() << "\n";
    }
    try {
        Baud rate{10000};
        configure_serial(rate);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Baud{10000} threw: " << e.what() << "\n";
    }
    try {
        Percent level{150};
        set_volume(level);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Percent{150} threw: " << e.what() << "\n";
    }
    try {
        BlockOffset offset{std::numeric_limits<int64_t>::max()};  // would overflow offset + size
        process_iq_block(offset, FFTSize{512});
    } catch (const std::invalid_argument& e) {
        std::cout << "  BlockOffset{INT64_MAX} threw: " << e.what() << "\n";
    }
    try {
        GpioPin pin{14};
        toggle_pin(pin);
    } catch (const std::invalid_argument& e) {
        std::cout << "  GpioPin{14} threw: " << e.what() << "\n";
    }
    try {
        BurstLength burst{96};
        start_dma(burst);
    } catch (const std::invalid_argument& e) {
        std::cout << "  BurstLength{96} threw: " << e.what() << "\n";
    }

    std::cout << "\n== Validated types inside structs ==\n";
    // Built from constants, every field is checked during the build
    constexpr RadioConfig defaults{ChannelID{14}, Baud{115200}, Percent{50}};
    std::cout << "compile-time defaults:\n";
    apply_config(defaults);

    // Built from a wire format, checked once in parse_config, then trusted.
    for (RadioConfigWire wire : {RadioConfigWire{2048, 38400, 80},
                                 RadioConfigWire{2048, 12345, 80}}) {
        std::cout << "wire {" << wire.channel << ", " << wire.baud << ", "
                  << int{wire.volume} << "}:\n";
        if (auto cfg = parse_config(wire)) {
            apply_config(*cfg);
        } else {
            std::cout << "  rejected\n";
        }
    }

    // A rule that spans fields, each part is valid, the pair may still not be.
    std::cout << "rules across fields:\n";
    constexpr IQBlock first_block{IQRange{BlockOffset{0}, FFTSize{4096}}};
    process_iq_block(first_block);
    try {
        IQBlock past_the_end{IQRange{BlockOffset{kCaptureBufferSamples - 100}, FFTSize{512}}};
        process_iq_block(past_the_end);
    } catch (const std::invalid_argument& e) {
        std::cout << "  IQBlock{1048476, 512} threw: " << e.what() << "\n";
    }

    std::cout << "\n== Several types, one pattern ==\n";
    process_iq_block(BlockOffset{10000}, FFTSize{512});
    configure_serial(Baud{38400});
    set_volume(Percent{80});
    toggle_pin(GpioPin{7});
    start_dma(BurstLength{32});
    start_dma(BurstLength{1000});

    std::cout << "\n== The rest of the toolbox ==\n";
    // Each line: a value that passes, then one that fails, for one rule.
    auto show = [](std::string_view name, auto make) {
        try {
            auto value = make();  // may throw, nothing printed until it succeeds
            std::cout << "  " << name << ": ok " << value << "\n";
        } catch (const std::invalid_argument& e) {
            std::cout << "  " << name << ": " << e.what() << "\n";
        }
    };
    show("DmaLength{4096}",   [] { return DmaLength{4096}; });
    show("DmaLength{4095}",   [] { return DmaLength{4095}; });
    show("Stride{-1}",        [] { return Stride{-1}; });
    show("Stride{0}",         [] { return Stride{0}; });
    show("DacValue{4095}",    [] { return DacValue{4095}; });
    show("DacValue{4096}",    [] { return DacValue{4096}; });
    show("LedMask{0b1010}",   [] { return LedMask{0b1010}; });
    show("LedMask{0b10000}",  [] { return LedMask{0b10000}; });
    show("BufferIndex{255}",  [] { return BufferIndex{255}; });
    show("BufferIndex{256}",  [] { return BufferIndex{256}; });
    show("Gain{2.5}",         [] { return Gain{2.5}; });
    show("Gain{NAN}",         [] { return Gain{NAN}; });
    show("PllDivider{1}",     [] { return PllDivider{1}; });
    show("PllDivider{7}",     [] { return PllDivider{7}; });

    return 0;
}
