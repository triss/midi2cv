# midi2cv diagrams

Hand-drawn SVGs of midi2cv's design — open any `.svg` in a browser or an
editor with SVG preview. All share one accessible visual style: light
background, high-contrast text, a colour-blind-safe palette, and shape cues
(solid = module/process, dashed = type/interface, dotted = porting seam) so
meaning never rests on colour alone.

## How the software works

- [01 · object model](01-object-model.svg) — modules and the data they own, and the two porting seams.
- [02 · signal flow](02-signal-flow.svg) — one JACK block, top to bottom: MIDI bytes in → calibrated volts out.
- [03 · engine_run & the render family](03-engine-run.svg) — the event walk, and the render-adapter dispatch.

## The same code, through different minds

- [04 · Hickey — simple, not easy](04-hickey-simple-vs-complected.svg) — concerns as separate strands vs. one complected knot.
- [05 · Hickey — value · place · identity · time](05-hickey-values-time.svg) — immutable values, an identity over time, contained mutable place.
- [06 · Kay — the big idea is messaging](06-kay-cells-and-messages.svg) — cells with membranes; `render` as one late-bound message.
- [07 · Kay — systems of systems](07-kay-internet-model.svg) — an object is a whole computer; the pattern repeats at every zoom.
- [08 · Minsky — a society of mindless agents](08-minsky-society-and-frames.svg) — emergent competence, and `cvout` as a frame of slots with defaults.
- [09 · McCarthy — config as program](09-mccarthy-config-as-program.svg) — `engine_init` = eval, `engine_run` = apply; the GC he'd notice missing.
- [10 · Turing — a machine with a tape](10-turing-machine.svg) — input tape, head, state register, transition rules, output tape.

## Why it belongs

- [11 · permacomputing](11-permacomputing.svg) — a small durable core that outlives its hardware, and which salvaged machines can run it with which adapter.

## If minimalism weren't the constraint

- [12 · patterns that could tidy it](12-patterns.svg) — design patterns that might clean the code up (even at the cost of length), each with an honest verdict on whether the structure pays.
