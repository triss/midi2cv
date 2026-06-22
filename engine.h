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

#define MAX_CHANNELS 64

typedef enum { T_PITCH, T_GATE, T_TRIG, T_VEL, T_CC, T_CLOCK } chan_type;

/* One output channel's configuration. The adapter parses these and hands them
 * to engine_init; runtime clock state (pulse width/countdown) is the engine's. */
typedef struct {
	char      name[64];   /* port short name (used by the adapter) */
	chan_type type;
	int       midich;     /* 0..15; unused for clock */
	int       param;      /* trig: note; cc: CC; clock: division; else -1 */
	float     scale;
	float     offset;
	float     pulse_ms;   /* clock: pulse width */
} channel;

/* A timestamped MIDI message, portable across the seam. data holds up to the
 * first 3 bytes; len is the real length (1 for system real-time). */
typedef struct {
	uint32_t time;        /* frame offset within the block */
	uint8_t  data[3];
	uint8_t  len;
} midi_ev;

/* (Re)initialise the engine for a fixed channel set. Copies the channels in,
 * derives clock pulse widths from sample_rate, and clears all playing state. */
void engine_init(const channel *chans, int n, uint32_t sample_rate);

/* Render one block. Events must be in time order with time < nframes. Fills
 * outs[0..nchannels-1][0..nframes-1]. Allocation- and lock-free. */
void engine_run(const midi_ev *evs, int nev, float *outs[], uint32_t nframes);

#endif /* ENGINE_H */
