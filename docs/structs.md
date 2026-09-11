# Validated types in structs

The snippets below assume `using namespace vetted;`, as the example code does.

They compose like any other member. A struct of validated fields carries the
same guarantee as its parts: if you have one, every field passed.

```cpp
struct RadioConfig {
    ChannelID channel;
    Baud      baud;
    Percent   volume;
};

constexpr RadioConfig defaults{ChannelID{14}, Baud{115200}, Percent{50}};  // checked at build time

void apply_config(const RadioConfig& cfg) {   // no checks, however deep it goes
    handle_request(cfg.channel);
    configure_serial(cfg.baud);
    set_volume(cfg.volume);
}
```

Layout is identical to the same struct with raw ints: same size, same
alignment, trivially copyable. `examples/domain.hpp` asserts this.

Bytes off a socket or out of flash can't carry a proof, so a packed wire
struct should stay raw. Convert it once, in one function:

```cpp
struct __attribute__((packed)) RadioConfigWire { int16_t channel; int32_t baud; int8_t volume; };

std::optional<RadioConfig> parse_config(const RadioConfigWire& wire) {
    auto channel = ChannelID::try_from(wire.channel);
    auto baud    = Baud::try_from(wire.baud);
    auto volume  = Percent::try_from(wire.volume);
    if (!channel || !baud || !volume) return std::nullopt;
    return RadioConfig{*channel, *baud, *volume};
}
```

A rule can also span several fields. `Validated<T>` works for any `T`, so
wrap a struct and write a rule that looks at the whole thing. In this example
each field is valid on its own, but the pair must also fit inside the capture
buffer, which neither field can promise alone:

```cpp
struct IQRange { BlockOffset offset; FFTSize size; };

struct WithinCaptureBuffer {
    static constexpr bool passes(const IQRange& r) { return r.offset + r.size <= kCaptureBufferSamples; }
    static std::string requirement() { return "offset + size <= " + std::to_string(kCaptureBufferSamples); }
};

using IQBlock = Validated<IQRange, WithinCaptureBuffer>;

void process_iq_block(const IQBlock& block) {
    int64_t end = block->offset + block->size;   // -> reaches the fields
}
```

A `Validated` field inside a packed struct also packs correctly on Clang.
GCC may refuse to under-align it, because the type has a user-provided
constructor and so isn't a POD, and it will warn "ignoring packed attribute
because of unpacked non-POD field". Keeping wire structs raw sidesteps that.
