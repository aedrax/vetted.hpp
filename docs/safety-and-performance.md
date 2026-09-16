# Is it safer? Is it faster?

Safer, yes. You cannot pass a raw `int16_t` where a `Quantity` is expected,
so a missing check is a compile error, not a bug that ships. The rule lives
on one line, so a change from 1000 to 2000 updates every layer at once. A
function that takes a `Quantity` states its assumptions in its signature,
which makes it easier to review. The only way in is the `explicit`
constructor, so every place a raw value becomes a trusted one is greppable.

Faster, a little, because there is less to do. This is the three-layer chain
compiled both ways at `-O2` on arm64:

| | Old way, per layer | Validated, per layer |
|---|---|---|
| Hot path | Range check, branch, tail call (6 instructions) | Sign-extend, tail call (2 instructions) |
| Cold path | An exception-throwing block in every function | None |
| Argument passing | `int16_t` in a register | `int16_t` in a register |

The wrapper is free. `Quantity` is the same size as `int16_t`, trivially
copyable and trivially destructible, so it passes in registers exactly like
the raw integer. A `constexpr Quantity` is checked at build time and emits no
code. The `requirement()` strings run only when a check fails. The success
path never touches them.

A correctly predicted branch is almost free on a modern CPU, so the gain per
call is small. It adds up in deep call chains and hot loops, not in code that
runs once. If the old code already validated only at the boundary, there is
nothing to remove. The reason to use this is safety. The speed is a side
effect.

## A rule promises exactly what it says

An early version of the example had `FileOffset = Validated<int64_t, AtLeast<0>>`
and then computed `offset + length`. `INT64_MAX` is a valid `FileOffset` under
that rule, and the addition overflowed. The type never claimed the sum would
fit. The function assumed it.

The fix is one of two things. Make the type promise what the function needs
(`FileOffset` is now bounded by `INT64_MAX - kMaxChunkSize`). Or validate the
pair together (`Slice`, see [structs.md](structs.md)).

Arithmetic on a validated value gives a plain `T`. That is correct, because
`offset + length` is not a `FileOffset`. But it means the proof does not
survive math. Reading out is free. Only going in is guarded.

## What keeps the guarantee intact

- There is no default constructor and there are no setters. Once built, the
  value cannot change into something invalid.
- The only way in is the `explicit` constructor, `try_from`, or implicit
  widening from a more refined type. Widening is safe because the source type
  already ran every target rule. If someone adds an "unchecked" constructor
  for convenience, the whole thing turns back into a plain `int`.
- The type says "this number is in range" and nothing else. It is not memory
  safety or thread safety.
