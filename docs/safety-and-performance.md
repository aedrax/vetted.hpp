# Is it safer? Is it faster?

Safer, yes. You cannot pass a raw `int16_t` where a `ChannelID` is expected,
so forgetting to validate is a compile error rather than a bug that ships. The
rule lives in one line, so changing 4096 to 8192 updates every layer at once.
A function that takes `ChannelID` is declaring its assumptions in its
signature, which makes it easier to review. And the only way in is the
`explicit` constructor, so every place a raw value becomes a trusted one is
greppable.

Faster, a little, and only because there is less to do. Compiling the
three-layer chain both ways at `-O2` on arm64:

| | Old way, per layer | Validated, per layer |
|---|---|---|
| Hot path | Range check, branch, tail call (6 instructions) | Sign-extend, tail call (2 instructions) |
| Cold path | An exception-throwing block in every function | None |
| Argument passing | `int16_t` in a register | `int16_t` in a register |

The wrapper is free: `ChannelID` is the same size as `int16_t`, trivially
copyable and trivially destructible, so it passes in registers exactly like
the raw integer. A `constexpr ChannelID` is checked at build time and emits no
code at all. The `requirement()` strings only run when a check fails; the success
path never touches them.

A correctly predicted branch is nearly free on a modern CPU, so the gain per
call is small. It adds up in deep call chains and hot loops, not in code that
runs once. And if the old code already validated only at the boundary, there
is nothing to remove. The reason to use this is safety. The speed is a side
effect.

## A rule promises exactly what it says

An early version of the example had `BlockOffset = Validated<int64_t, AtLeast<0>>`
and then computed `offset + size`. `INT64_MAX` is a perfectly valid
`BlockOffset` under that rule, and the addition overflowed. The type never
claimed the sum would fit; the function assumed it.

The fix is either to make the type promise what the function needs
(`BlockOffset` is now bounded by `INT64_MAX - kMaxFFTSize`) or to validate
the pair together (`IQBlock`, see [structs.md](structs.md)).

Arithmetic on a validated value produces a plain `T`. That is correct, since
`offset + size` is not a `BlockOffset`, but it means the proof doesn't
propagate through math. Reading out is free and only going in is guarded.

## What keeps the guarantee intact

- There is no default constructor and there are no setters, so once built the
  value can't be changed into something invalid.
- The only way in is the `explicit` constructor or `try_from`. If someone adds
  an "unchecked" constructor for convenience, the whole thing quietly turns
  back into a plain `int`.
- The type says "this number is in range" and nothing else; it is not memory
  safety or thread safety.
