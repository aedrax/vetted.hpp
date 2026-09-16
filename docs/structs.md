# Validated types in structs

The snippets below assume `using namespace vetted;`, as the example code does.

Validated types compose like any other member. A struct of validated fields
carries the same guarantee as its parts. If you have one, every field passed.

```cpp
struct RadioConfig {
    ChannelID channel;
    Baud      baud;
    Percent   volume;
};

constexpr RadioConfig defaults{ChannelID{14}, Baud{115200}, Percent{50}};  // checked at build time

void apply_config(const RadioConfig& cfg) {   // no checks, at any depth
    handle_request(cfg.channel);
    configure_serial(cfg.baud);
    set_volume(cfg.volume);
}
```

The layout is identical to the same struct with raw ints: same size, same
alignment, trivially copyable. `examples/domain.hpp` asserts this.

Bytes off a socket or out of flash cannot carry a proof, so keep a packed
wire struct raw. Convert it once, in one function:

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
wrap a struct and write a rule that reads the whole thing. In this example,
each field is valid on its own, but the pair must also fit inside the capture
buffer. Neither field can promise that alone:

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

A `Validated` field inside a packed struct packs correctly on Clang. GCC may
refuse to under-align it, because the type has a user-provided constructor
and is not a POD. GCC then warns "ignoring packed attribute because of
unpacked non-POD field". A raw wire struct avoids this.
