# tools

Throwaway helpers for testing `midi2cv` against a live JACK server. Not built
by the main `Makefile`.

## clockgen.c

A minimal JACK MIDI client that emits a `0xFA` Start then a `0xF8` timing clock
every N frames — a stand-in for a sequencer's clock, used to test the `clock`
channel type.

```
cc -O2 -Wall -Wextra tools/clockgen.c -ljack -o /tmp/clockgen
/tmp/clockgen 1000          # one clock every 1000 frames
jack_connect clockgen:out clock:midi_in
```

## analyze.py

Reads a 16-bit WAV capture and reports, per channel, the pulse count, mean
pulse width and mean interval (in frames and ms). Needs `numpy`.

```
jack_capture -b 16 --port clock:quarter --port clock:sixteenth -d 2 cap.wav
python3 tools/analyze.py cap.wav quarter,sixteenth
```

## Example: verifying the clock divisions

```
./midi2cv examples/clock.conf &
/tmp/clockgen 1000 &
sleep 1; jack_connect clockgen:out clock:midi_in
jack_capture -b 16 --port clock:quarter --port clock:sixteenth -d 2 /tmp/cap.wav
python3 tools/analyze.py /tmp/cap.wav quarter,sixteenth
# quarter (div 24): interval ~24000 fr; sixteenth (div 6): ~6000 fr
```
