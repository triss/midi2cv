# midi2cv — domain glossary

The ubiquitous language for this project. Architecture vocabulary (module,
seam, deep, adapter, leverage, locality) lives in the `/codebase-design` skill;
this file names the **domain**. The same words appear in the code: `cvout`,
`source`, `engine`, `config`.

## engine

The JACK-free MIDI→CV core: all musical state (note stacks, CC values, clock
transport, per-CV-output pulse countdown) plus the calibrated sample mapping,
behind one small interface — `engine_init(cvouts, n, sample_rate)` then
`engine_run(events, nevents, outs, nframes)` per block. `engine_run` owns the
**sample-accurate interleave** (fill each output up to an event's frame, apply
it, continue). Lives in `engine.c` / `engine.h` with no JACK dependency, so it
is tested directly (`test_engine.c`, `make test`) without a running server. The
JACK process callback is a thin **adapter** over it that copies JACK events
into `midi_ev` and gathers the output buffers.

## backend adapter

The thin, backend-specific layer that drives the engine: it owns the audio
client/ports, copies incoming MIDI into `midi_ev`, gathers the planar `float`
output buffers, calls `engine_run` per block, and runs until a signal. It sits
behind one entry point, `backend_run(cfg, test)` (`backend.h`), which the
program shell (`cli.c`) calls after parsing argv and loading the config.
`jack.c` is the current adapter. **Porting is writing a new `backend_run`** —
the shell, engine, config, and tests do not change, because no backend types
cross the engine interface (only `midi_ev`, planar `float`, and a sample rate)
or the `backend_run` seam (only `config` and `test_req`). Sample-accuracy needs
the backend to stamp MIDI events in frames; otherwise the adapter falls back to
block-granular timing. The DC-coupled output is physical — avoid
shared/resampled paths that mangle DC.

## CV output

One control-voltage output: a single JACK audio port driven by one MIDI
**source** and mapped to a float sample via two **calibration constants**. The
unit of configuration (code: `cvout`). Named "CV output", never "channel" —
"channel" in this project means only a **MIDI channel** (`midich`, 1–16), which
a CV output *has*, so reusing the word for the unit itself would collide.

## source

What a CV output reads from MIDI (code: the `source` enum, the `src` field):
`pitch`, `gate`, `trig`, `vel`, `cc`, `clock`. Each is a render adapter behind
one interface, so adding a source is one function. (Previously called "channel
type" — renamed to remove the "channel" collision.)

## calibration constants

Per-CV-output `scale` and `offset`: `sample = offset + value * scale` (`pitch`
uses the semitone index as `value`). Per-output because each ES-3 channel has
its own small offset/scale error.

## note stack / last-note priority

Per-MIDI-channel stack of held notes; the newest wins (mono). `pitch` holds its
last value after release; `gate` falls.

## transport

MIDI system real-time clock state. Start (`0xFA`) resets the divider phase,
Stop (`0xFC`) drops clock outputs low, Continue (`0xFB`) resumes. Runs by
default so bare clock sources work.

## ES-3

Expert Sleepers module converting DC-coupled audio sample values to CV.
