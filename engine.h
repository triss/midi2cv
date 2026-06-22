/*
 * engine - the JACK-free MIDI->CV core (see CONTEXT.md "engine").
 *
 * Holds all musical state and the calibrated sample mapping behind one small
 * interface. engine_run owns the sample-accurate interleave. No JACK types
 * appear here, so the engine builds and tests without a server.
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

#define MAX_CVOUTS 64

typedef enum { S_PITCH, S_GATE, S_TRIG, S_VEL, S_CC, S_CLOCK } source;

/* One CV output's configuration. The adapter parses these and hands them to
 * engine_init; runtime clock state (pulse width/countdown) is the engine's. */
typedef struct {
	char   name[64];      /* port short name (used by the adapter) */
	source src;           /* what MIDI this output reads */
	int    midich;        /* 0..15; unused for clock */
	int    param;         /* trig: note; cc: CC; clock: division; else -1 */
	float  scale;
	float  offset;
	float  pulse_ms;      /* clock: pulse width */
} cvout;

/* A timestamped MIDI message, portable across the seam. data holds up to the
 * first 3 bytes; len is the real length (1 for system real-time). */
typedef struct {
	uint32_t time;        /* frame offset within the block */
	uint8_t  data[3];
	uint8_t  len;
} midi_ev;

/* (Re)initialise the engine for a fixed set of CV outputs. Copies them in,
 * derives clock pulse widths from sample_rate, and clears all playing state. */
void engine_init(const cvout *cvouts, int n, uint32_t sample_rate);

/* Render one block. Events must be in time order with time < nframes. Fills
 * outs[0..ncvouts-1][0..nframes-1]. Allocation- and lock-free. */
void engine_run(const midi_ev *evs, int nev, float *outs[], uint32_t nframes);

#endif /* ENGINE_H */
