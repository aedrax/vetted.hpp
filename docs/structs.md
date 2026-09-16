# Validated types in structs

The snippets below assume `using namespace vetted;`, as the example code does.

Validated types compose like any other member. A struct of validated fields
carries the same guarantee as its parts. If you have one, every field passed.

```cpp
struct Order {
    Quantity     quantity;
    ShippingDays shipping;
    Percent      discount;
};

constexpr Order defaults{Quantity{1}, ShippingDays{5}, Percent{0}};  // checked at build time

void place_order(const Order& order) {   // no checks, at any depth
    handle_request(order.quantity);
    choose_shipping(order.shipping);
    apply_discount(order.discount);
}
```

The layout is identical to the same struct with raw ints: same size, same
alignment, trivially copyable. `examples/domain.hpp` asserts this.

Bytes off a socket or out of a file cannot carry a proof, so keep a packed
wire struct raw. Convert it once, in one function:

```cpp
struct __attribute__((packed)) OrderWire { int16_t quantity; int32_t shipping; int8_t discount; };

std::optional<Order> parse_order(const OrderWire& wire) {
    auto quantity = Quantity::try_from(wire.quantity);
    auto shipping = ShippingDays::try_from(wire.shipping);
    auto discount = Percent::try_from(wire.discount);
    if (!quantity || !shipping || !discount) return std::nullopt;
    return Order{*quantity, *shipping, *discount};
}
```

A rule can also span several fields. `Validated<T>` works for any `T`, so
wrap a struct and write a rule that reads the whole thing. In this example,
each field is valid on its own, but the pair must also fit inside the largest
file the service stores. Neither field can promise that alone:

```cpp
struct FileRange { FileOffset offset; ChunkSize length; };

struct WithinFile {
    static constexpr bool passes(const FileRange& r) { return r.offset + r.length <= kMaxFileBytes; }
    static std::string requirement() { return "offset + length <= " + std::to_string(kMaxFileBytes); }
};

using Slice = Validated<FileRange, WithinFile>;

void read_slice(const Slice& slice) {
    int64_t end = slice->offset + slice->length;   // -> reaches the fields
}
```

A `Validated` field inside a packed struct packs correctly on Clang. GCC may
refuse to under-align it, because the type has a user-provided constructor
and is not a POD. GCC then warns "ignoring packed attribute because of
unpacked non-POD field". A raw wire struct avoids this.
