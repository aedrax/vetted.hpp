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
// The call chain
// ============================================================================

namespace the_old_way {
// An order passes through three layers of functions, and each layer repeats
// the same check. No layer can trust its caller, so every layer is defensive.
// Kept here only for contrast.

// This one uses the value, so it checks. Fair enough.
void reserve_stock(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    // ... stuff ...
    std::cout << "  reserved " << quantity << " in stock\n";
}

// Only forwards, but cannot trust its caller, so it checks again.
void add_to_cart(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    // ... stuff ...
    reserve_stock(quantity);
}

// Top of the chain. The first place that validates the input.
void handle_request(int16_t quantity) {
    if (quantity < 1 || quantity > 1000) throw std::invalid_argument("bad quantity");
    // ... stuff ...
    add_to_cart(quantity);
}
}  // namespace the_old_way

// The same three layers with Quantity, a Validated type. No layer checks,
// because a Quantity cannot exist unless the checks already passed. Each
// function forwards the value and does its job.

void reserve_stock(Quantity quantity) {
    // ... stuff ...
    std::cout << "  reserved " << quantity << " in stock\n";
}

void add_to_cart(Quantity quantity) {
    // ... stuff ...
    std::cout << "  added " << quantity << " to the cart\n";
    reserve_stock(quantity);
}

void handle_request(Quantity quantity) {
    // ... stuff ...
    std::cout << "  request for " << quantity << " items\n";
    add_to_cart(quantity);
}

// A refined type. Only gift wrap needs the extra rule. Everything below it
// still takes a plain Quantity.
void gift_wrap(GiftQuantity quantity) {
    std::cout << "  gift wrap for " << quantity << " items\n";
    reserve_stock(quantity);  // GiftQuantity -> Quantity: implicit, nothing to check
}

// More functions that take validated types and do math on them.

// Arithmetic on validated values. FileOffset is bounded, so the sum cannot overflow.
void read_slice(FileOffset offset, ChunkSize length) {
    int64_t end = offset + length;  // cannot overflow: FileOffset is bounded for this
    std::cout << "  reading bytes [" << offset << ", " << end << ")\n";
}

// A validated struct. One rule spans both fields, so the slice is known to fit.
void read_slice(const Slice& slice) {
    int64_t end = slice->offset + slice->length;
    std::cout << "  reading bytes [" << slice->offset << ", " << end << ") of the file\n";
}

// A fixed set of allowed values (In<1, 2, 5>).
void choose_shipping(ShippingDays days) {
    std::cout << "  ships in " << days << " days\n";
}

// A closed range (Between<0, 100>).
void apply_discount(Percent discount) {
    std::cout << "  discount of " << discount << "%\n";
}

// A range with holes (Between<-2, 50> plus NotIn<0, 13>).
void call_elevator(Floor floor) {
    std::cout << "  elevator to floor " << floor << "\n";
}

// Nested combinators (AnyOf<AllOf<MultipleOf<5>, AtMost<50>>, In<100>>).
void apply_coupon(Coupon coupon) {
    std::cout << "  coupon for " << coupon << "% off\n";
}

// A struct of validated fields. If you have one, every field passed, so there are no checks.
void place_order(const Order& order) {
    handle_request(order.quantity);
    choose_shipping(order.shipping);
    apply_discount(order.discount);
}

// ============================================================================
// The boundary: the one place where validation is visible
// ============================================================================

// Untrusted text comes in. A Quantity, or nothing, goes out. Everything
// below this function gets a value it can trust.

std::optional<Quantity> parse_quantity(std::string_view text) {
    int16_t raw{};
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), raw);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;  // not a number at all
    }
    return Quantity::try_from(raw);  // a number, but does it pass the rules?
}

// The same idea for a struct. A packed wire format holds raw integers, because
// bytes off a socket or out of a file cannot carry a proof. One conversion
// function turns it into an Order, or nothing. Code below it never sees the
// raw version.
struct __attribute__((packed)) OrderWire {
    int16_t quantity;
    int32_t shipping;
    int8_t  discount;
};
static_assert(sizeof(OrderWire) == 7);

std::optional<Order> parse_order(const OrderWire& wire) {
    auto quantity = Quantity::try_from(wire.quantity);
    auto shipping = ShippingDays::try_from(wire.shipping);
    auto discount = Percent::try_from(wire.discount);
    if (!quantity || !shipping || !discount) return std::nullopt;
    return Order{*quantity, *shipping, *discount};
}

// ============================================================================
// Demo
// ============================================================================

int main() {
    std::cout << "== Compile time: the compiler proves the constant is valid ==\n";
    std::cout << "== a Quantity is positive and at most 1000 ==\n";
    constexpr Quantity default_quantity{1};  // checked during the build
    static_assert(default_quantity == 1);
    handle_request(default_quantity);

#ifdef DEMO_COMPILE_ERROR
    // Build with -DDEMO_COMPILE_ERROR=ON to see this line fail to compile.
    // The diagnostic names the broken rule: rule_violated<AtMost<1000>>.
    constexpr Quantity never_compiles{5000};
#endif

    std::cout << "\n== Runtime: untrusted input is checked once, at the boundary ==\n";
    std::cout << "== each input becomes a Quantity, or is rejected ==\n";
    for (std::string_view input : {"250", "5000", "0", "abc"}) {
        std::cout << "input \"" << input << "\":\n";
        if (auto quantity = parse_quantity(input)) {
            handle_request(*quantity);  // three layers deep, zero re-checks
        } else {
            std::cout << "  rejected\n";
        }
    }

    std::cout << "\n== The throwing constructor, for values that must never be wrong ==\n";
    std::cout << "== these could be constexpr, but the demo needs them to throw ==\n";
    try {
        ChunkSize chunk{1000};
        read_slice(FileOffset{0}, chunk);
    } catch (const std::invalid_argument& e) {
        std::cout << "  ChunkSize{1000} threw: " << e.what() << "\n";
    }
    try {
        ShippingDays days{3};
        choose_shipping(days);
    } catch (const std::invalid_argument& e) {
        std::cout << "  ShippingDays{3} threw: " << e.what() << "\n";
    }
    try {
        Percent discount{150};
        apply_discount(discount);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Percent{150} threw: " << e.what() << "\n";
    }
    try {
        FileOffset offset{std::numeric_limits<int64_t>::max()};  // would overflow offset + length
        read_slice(offset, ChunkSize{512});
    } catch (const std::invalid_argument& e) {
        std::cout << "  FileOffset{INT64_MAX} threw: " << e.what() << "\n";
    }
    try {
        Floor floor{13};
        call_elevator(floor);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Floor{13} threw: " << e.what() << "\n";
    }
    try {
        Coupon coupon{7};
        apply_coupon(coupon);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Coupon{7} threw: " << e.what() << "\n";
    }

    std::cout << "\n== Validated types inside structs ==\n";
    // Built from constants. Every field is checked during the build.
    constexpr Order defaults{Quantity{1}, ShippingDays{5}, Percent{0}};
    std::cout << "compile-time defaults:\n";
    place_order(defaults);

    // Built from a wire format, checked once in parse_order, then trusted.
    for (OrderWire wire : {OrderWire{250, 2, 10},
                           OrderWire{250, 3, 10}}) {
        std::cout << "wire {" << wire.quantity << ", " << wire.shipping << ", "
                  << int{wire.discount} << "}:\n";
        if (auto order = parse_order(wire)) {
            place_order(*order);
        } else {
            std::cout << "  rejected\n";
        }
    }

    // A rule that spans fields. Each part is valid. The pair may still fail.
    std::cout << "rules across fields:\n";
    constexpr Slice first_slice{FileRange{FileOffset{0}, ChunkSize{4096}}};
    read_slice(first_slice);
    try {
        Slice past_the_end{FileRange{FileOffset{kMaxFileBytes - 100}, ChunkSize{512}}};
        read_slice(past_the_end);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Slice{1048476, 512} threw: " << e.what() << "\n";
    }

    std::cout << "\n== Refining a type ==\n";
    constexpr GiftQuantity gift{2};
    handle_request(gift);  // a GiftQuantity is a Quantity, so the whole chain accepts it
    for (Quantity quantity : {Quantity{2}, Quantity{250}}) {
        // Narrowing runs only the rule Quantity did not prove, AtMost<5>.
        if (auto narrowed = GiftQuantity::try_from(quantity)) {
            gift_wrap(*narrowed);
        } else {
            std::cout << "  " << quantity << " items is too many for gift wrap\n";
        }
    }

    std::cout << "\n== Several types, one pattern ==\n";
    read_slice(FileOffset{10000}, ChunkSize{512});
    choose_shipping(ShippingDays{2});
    apply_discount(Percent{10});
    call_elevator(Floor{7});
    apply_coupon(Coupon{25});
    apply_coupon(Coupon{100});

    std::cout << "\n== The rest of the toolbox ==\n";
    // Two lines per rule: a value that passes, then one that fails.
    auto show = [](std::string_view name, auto make) {
        try {
            auto value = make();  // may throw. Nothing prints until it succeeds.
            std::cout << "  " << name << ": ok";
            if constexpr (requires { std::cout << value; }) std::cout << " " << value;
            std::cout << "\n";
        } catch (const std::invalid_argument& e) {
            std::cout << "  " << name << ": " << e.what() << "\n";
        }
    };
    show("SlotMinutes{45}",     [] { return SlotMinutes{45}; });
    show("SlotMinutes{50}",     [] { return SlotMinutes{50}; });
    show("DiskOffset{4096}",    [] { return DiskOffset{4096}; });
    show("DiskOffset{4095}",    [] { return DiskOffset{4095}; });
    show("Step{-1}",            [] { return Step{-1}; });
    show("Step{0}",             [] { return Step{0}; });
    show("ColorChannel{255}",   [] { return ColorChannel{255}; });
    show("ColorChannel{256}",   [] { return ColorChannel{256}; });
    show("Permissions{0b101}",  [] { return Permissions{0b101}; });
    show("Permissions{0b1000}", [] { return Permissions{0b1000}; });
    show("RowIndex{255}",       [] { return RowIndex{255}; });
    show("RowIndex{256}",       [] { return RowIndex{256}; });
    show("Price{9.99}",         [] { return Price{9.99}; });
    show("Price{NAN}",          [] { return Price{NAN}; });
    show("PlayerCount{1}",      [] { return PlayerCount{1}; });
    show("PlayerCount{7}",      [] { return PlayerCount{7}; });
    show("RetryDelay{0}",       [] { return RetryDelay{0}; });
    show("RetryDelay{-5}",      [] { return RetryDelay{-5}; });
    show("ImageWidth{1920}",    [] { return ImageWidth{1920}; });
    show("ImageWidth{1919}",    [] { return ImageWidth{1919}; });
    show("PanelSize{5}",        [] { return PanelSize{5}; });
    show("PanelSize{6}",        [] { return PanelSize{6}; });
    show("Temperature{-40}",    [] { return Temperature{-40}; });
    show("Temperature{200}",    [] { return Temperature{200}; });
    show("SharePerms{0x5}",     [] { return SharePerms{0x5}; });
    show("SharePerms{0x4}",     [] { return SharePerms{0x4}; });
    show("Locale{\"en-US\"}",   [] { return Locale{"en-US"}; });
    show("Locale{nullptr}",     [] { return Locale{nullptr}; });

    std::cout << "\n== Sizes and text ==\n";
    // The same rules at compile time. The compiler parses the address.
    constexpr ServerAddress loopback{"::1"};
    constexpr WebhookUrl default_hook{"https://example.org/hooks/orders"};
    std::cout << "  compile-time: " << loopback << ", " << default_hook << "\n";
    show("Attachment{1, 2, 3}",   [] { return Attachment{{1, 2, 3}}; });
    show("Attachment{}",          [] { return Attachment{{}}; });
    show("Scores{90, 100, 0}",    [] { return Scores{{90, 100, 0}}; });
    show("Scores{90, 101}",       [] { return Scores{{90, 101}}; });
    show("Milestones{25, 50, 100}", [] { return Milestones{{Percent{25}, Percent{50}, Percent{100}}}; });
    show("Milestones{25, 100, 50}", [] { return Milestones{{Percent{25}, Percent{100}, Percent{50}}}; });
    show("Milestones{25, 50, 50}",  [] { return Milestones{{Percent{25}, Percent{50}, Percent{50}}}; });
    show("ListenPort{8080}",      [] { return ListenPort{8080}; });
    show("ListenPort{0}",         [] { return ListenPort{0}; });
    show("DisplayName{\"Ada Lovelace\"}",   [] { return DisplayName{"Ada Lovelace"}; });
    show("DisplayName{\"Ada\\tLovelace\"}", [] { return DisplayName{"Ada\tLovelace"}; });
    show("DeviceMac{\"00:1a:2b:3c:4d:5e\"}", [] { return DeviceMac{"00:1a:2b:3c:4d:5e"}; });
    show("DeviceMac{\"00:1a:2b:3c:4d\"}",    [] { return DeviceMac{"00:1a:2b:3c:4d"}; });
    show("SessionId{\"123e4567-e89b-12d3-a456-426614174000\"}",
         [] { return SessionId{"123e4567-e89b-12d3-a456-426614174000"}; });
    show("SessionId{\"123e4567\"}", [] { return SessionId{"123e4567"}; });
    show("Comment{\"caf\\xC3\\xA9\"}", [] { return Comment{"caf\xC3\xA9"}; });
    show("Comment{\"caf\\xC3\"}",       [] { return Comment{"caf\xC3"}; });  // truncated sequence
    show("CountryCode{\"US\"}",   [] { return CountryCode{"US"}; });
    show("CountryCode{\"USA\"}",  [] { return CountryCode{"USA"}; });
    show("Username{\"ada_1815\"}", [] { return Username{"ada_1815"}; });
    show("Username{\"Ada\"}",      [] { return Username{"Ada"}; });
    show("ReportName{\"report-2026-q3.pdf\"}",  [] { return ReportName{"report-2026-q3.pdf"}; });
    show("ReportName{\"report-2026-q3.docx\"}", [] { return ReportName{"report-2026-q3.docx"}; });
    show("ApiHost{\"api.example.org\"}", [] { return ApiHost{"api.example.org"}; });
    show("ApiHost{\"api example.org\"}", [] { return ApiHost{"api example.org"}; });
    show("ServerAddress{\"192.0.2.1\"}",    [] { return ServerAddress{"192.0.2.1"}; });
    show("ServerAddress{\"192.0.2.256\"}",  [] { return ServerAddress{"192.0.2.256"}; });
    show("ServerAddress{\"2001:db8::1\"}",  [] { return ServerAddress{"2001:db8::1"}; });
    show("ServerAddress{\"2001:db8:::1\"}", [] { return ServerAddress{"2001:db8:::1"}; });
    show("ContactEmail{\"ada@example.org\"}", [] { return ContactEmail{"ada@example.org"}; });
    show("ContactEmail{\"ada@example\"}",     [] { return ContactEmail{"ada@example"}; });
    show("WebhookUrl{\"https://example.org/hooks\"}", [] { return WebhookUrl{"https://example.org/hooks"}; });
    show("WebhookUrl{\"http://example.org/hooks\"}",  [] { return WebhookUrl{"http://example.org/hooks"}; });
    show("WebhookUrl{\"example.org/hooks\"}",         [] { return WebhookUrl{"example.org/hooks"}; });

    return 0;
}
