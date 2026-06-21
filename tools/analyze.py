#!/usr/bin/env python3
"""Report pulse count, width and interval per channel of a 16-bit WAV capture.

Usage: analyze.py <capture.wav> <name1,name2,...>
Capture with: jack_capture -b 16 --port <client>:<port> ... out.wav
"""
import sys, wave, numpy as np

w = wave.open(sys.argv[1], 'rb')
sr = w.getframerate(); nch = w.getnchannels()
raw = w.readframes(w.getnframes())
a = np.frombuffer(raw, dtype='<i2').astype(float) / 32768.0
a = a.reshape(-1, nch)
names = sys.argv[2].split(',')

print(f"samplerate={sr} channels={nch} dur={a.shape[0]/sr:.2f}s")
for ch in range(nch):
    x = a[:, ch]
    hi = x > 0.25                      # high when near 0.5
    rises = np.where((~hi[:-1]) & hi[1:])[0] + 1
    falls = np.where(hi[:-1] & (~hi[1:]))[0] + 1
    widths = []
    for r in rises:
        f = falls[falls > r]
        if len(f): widths.append((f[0] - r))
    iv = np.diff(rises)
    nm = names[ch] if ch < len(names) else f"ch{ch}"
    pw = np.mean(widths) if widths else 0
    print(f"  {nm:9s} pulses={len(rises):2d} "
          f"width~{pw:.0f}fr ({pw/sr*1000:.2f}ms) "
          f"interval~{np.mean(iv):.0f}fr ({np.mean(iv)/sr*1000:.1f}ms)"
          if len(iv) else f"  {nm}: pulses={len(rises)}")
