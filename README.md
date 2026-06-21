# midi2cv

A lightweight MIDI-to-CV converter for a modular synth, built on JACK.

It takes MIDI in and drives Expert Sleepers **ES-3** CV outputs through your
soundcard's DC-coupled audio outputs (soundcard → ES-3 → modular). The ES-3
turns digital audio sample values into DC voltages, so this program just emits
the right constant float per output.

One running instance = one config file describing a flat list of **output
channels**. Each channel is a single JACK audio port driven by one MIDI source
and calibrated with two constants. Run several instances (different configs,
different JACK client names) for several setups at once.

## Why this exists / alternatives

If you want MIDI→CV on Linux *without writing code*, look at these first:

- **[Cardinal](https://github.com/DISTRHO/Cardinal)** — free, open-source VCV
  Rack as an LV2/VST/CLAP plugin. Ships Expert Sleepers + Silent Way modules,
  so you can patch MIDI → 1V/oct + gate and route it to the ES-3's audio
  channels. Closest "just works" option, but it's a full modular GUI.
- **Bitwig Studio** — native CV-out instrument, great with the ES-3, but
  commercial.
- **[Silent Way](https://www.expert-sleepers.co.uk/silentway.html)** — the
  canonical tool, but a VST/AU plugin aimed at Win/Mac (runs on Linux only via
  Carla/yabridge, with mixed results).
- **ams-lv2 `ControlToCV`** — small LV2, but converts MIDI **CC** only, not
  note→pitch/gate.

Most GitHub projects named "MIDI2CV" are *hardware* (Arduino/Teensy/Pi-Pico
firmware driving a DAC), not JACK software.

`midi2cv` fills the gap none of those cover: a **minimal, headless,
config-driven JACK daemon** — no GUI, no plugin host, plain-text configs,
multiple instances, runs on a Pi. Use it if that's the point; otherwise reach
for Cardinal.

## Build

Needs `libjack-dev` (JACK) and a C compiler.

```
make
```

Builds with `-Wall -Wextra`; expect zero warnings.

## Run

Start a JACK server first (e.g. via `qjackctl`, or `jackd -d alsa ...`), then:

```
./midi2cv examples/melodic.conf
```

The client registers a `midi_in` port plus one audio output port per channel.
Patch MIDI into `midi_in` and the audio outs to the soundcard channels feeding
the ES-3 (in `qjackctl`, or with `connect` lines in the config — see below).

```
jack_lsp            # list the ports it created
```

## Config format

Line-oriented, `#` starts a comment. Three kinds of line:

```
name      <client>          # JACK client name (default: midi2cv)
connect   <port> <target>   # optional: auto-connect an output at startup

# channel: <port> <type> <midich> <param> <scale> <offset>
pitch1    pitch    1   -    0.008333  0.0
gate1     gate     1   -    0.5       0.0
kick      trig     10  36   0.5       0.0
```

Channel fields:

| field    | meaning                                                       |
|----------|---------------------------------------------------------------|
| `port`   | JACK output port short name                                    |
| `type`   | `pitch` / `gate` / `trig` / `vel` / `cc` (see below)          |
| `midich` | MIDI channel, 1–16                                             |
| `param`  | `trig`: note number; `cc`: CC number; `-` for the rest        |
| `scale`  | calibration: volts-per-unit term                              |
| `offset` | calibration: zero-point term                                  |

Channel types and the logical value they map:

| type    | source                                   | value                       |
|---------|------------------------------------------|-----------------------------|
| `pitch` | top held note on its MIDI channel (mono) | semitone index (held on release) |
| `gate`  | any note held on its MIDI channel        | 1 held / 0 released         |
| `trig`  | one specific note (`param`)              | 1 that note held / 0        |
| `vel`   | velocity of the last note-on             | velocity / 127              |
| `cc`    | a specific CC (`param`)                  | cc value / 127              |

Output sample:

- `pitch`: `sample = offset + semitone * scale`
- all others: `sample = offset + value * scale`

Note priority is mono, last-note: the newest held note wins, and `pitch` holds
its last value after release (the `gate` goes low). Event timing is
sample-accurate: transitions land on the exact frame of the MIDI event within
the JACK period, not quantised to the block boundary.

## Calibration

Each ES-3 output has its own small offset/scale error, so calibrate per
channel. As a starting point, ~`0.1` float ≈ `1V` (full scale `1.0` ≈ ~10V),
so for 1V/octave `scale ≈ 0.1/12 ≈ 0.008333` and gate `scale = 0.5` ≈ 5V.

`--test` holds a single named output at a literal sample value so you can
measure the resulting volts and back out the constants:

```
./midi2cv --test examples/melodic.conf pitch1=0.5
```

Procedure for a `pitch` channel:

1. Patch the CV out to a VCO, and tune/measure the VCO (tuner or voltmeter).
2. `--test` a low sample, note the result; `--test` a sample one octave higher
   and adjust `scale` until 12 semitones span exactly 1V.
3. Adjust `offset` so a reference note reads the correct pitch.
4. Save the resulting `scale`/`offset` into the config.

## Examples

- `examples/melodic.conf` — mono voice: pitch + gate + velocity + mod wheel.
- `examples/drums.conf` — eight drum triggers on MIDI channel 10.

## Not in v1 (possible extensions)

- Pitch-bend / mod folded into `pitch` (bend state is already captured).
- Slew / glide smoothing for `cc`.
- A `clock` channel type: MIDI-clock divisions with per-channel pulse width,
  phase-reset on Start and low on Stop.
- Polyphonic voice allocation.
