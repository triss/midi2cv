/*
 * engine - MIDI->CV core. See engine.h and CONTEXT.md.
 *
 * Layout: state -> note stack -> sample mapping -> clock -> run.
 */
#include <string.h>

#include "engine.h"

/* ------------------------------------------------------------------ state */

static channel chans[MAX_CHANNELS];
static int     nchannels;
static int     pulse_samples[MAX_CHANNELS]; /* clock: width in frames */
static int     pulse_left[MAX_CHANNELS];    /* clock: frames left in pulse */

/* Per-MIDI-channel playing state. Fixed size, no allocation at run time. */
typedef struct {
	uint8_t stack[128];    /* held notes, last = highest priority */
	int     n;             /* number of held notes */
	int     last_note;     /* top note, held after release (pitch S&H) */
	uint8_t velocity;      /* velocity of the most recent note-on */
	uint8_t cc[128];       /* last value of every CC */
} mchan;

static mchan state[16];

/* MIDI-clock transport. Runs by default so bare clock sources work; Start
 * resets phase, Stop halts, Continue resumes. */
static struct { int running; uint32_t tick; } clk;

/* -------------------------------------------------------------- note stack */

static void note_on(mchan *m, uint8_t note, uint8_t vel)
{
	int i;
	m->velocity = vel;
	for (i = 0; i < m->n; i++)        /* already held: move to top */
		if (m->stack[i] == note) {
			memmove(&m->stack[i], &m->stack[i + 1], m->n - i - 1);
			m->n--;
			break;
		}
	if (m->n < 128)
		m->stack[m->n++] = note;
	m->last_note = note;
}

static void note_off(mchan *m, uint8_t note)
{
	int i;
	for (i = 0; i < m->n; i++)
		if (m->stack[i] == note) {
			memmove(&m->stack[i], &m->stack[i + 1], m->n - i - 1);
			m->n--;
			break;
		}
	if (m->n > 0)
		m->last_note = m->stack[m->n - 1];
}

static int is_held(const mchan *m, uint8_t note)
{
	int i;
	for (i = 0; i < m->n; i++)
		if (m->stack[i] == note)
			return 1;
	return 0;
}

/* Map a channel's current logical value to a calibrated float sample. */
static float channel_sample(const channel *c)
{
	const mchan *m = &state[c->midich];
	float value;

	switch (c->type) {
	case T_PITCH:                                   /* semitone index */
		return c->offset + m->last_note * c->scale;
	case T_GATE: value = m->n > 0 ? 1.0f : 0.0f;            break;
	case T_TRIG: value = is_held(m, c->param) ? 1.0f : 0.0f; break;
	case T_VEL:  value = m->velocity / 127.0f;             break;
	case T_CC:   value = m->cc[c->param] / 127.0f;         break;
	case T_CLOCK:                                   /* driven in fill loop */
	default:     value = 0.0f;                             break;
	}
	return c->offset + value * c->scale;
}

/* Write every output's current sample into frames [from, to). */
static void fill_segment(float *outs[], uint32_t from, uint32_t to)
{
	int i;
	uint32_t f;
	for (i = 0; i < nchannels; i++) {
		channel *c = &chans[i];
		if (c->type == T_CLOCK) {       /* per-sample pulse countdown */
			float hi = c->offset + c->scale, lo = c->offset;
			for (f = from; f < to; f++)
				outs[i][f] = pulse_left[i] > 0
					? (pulse_left[i]--, hi) : lo;
		} else {
			float s = channel_sample(c);
			for (f = from; f < to; f++)
				outs[i][f] = s;
		}
	}
}

/* ------------------------------------------------------------------ clock */

/* One MIDI timing clock (0xF8): start a fresh pulse on every clock channel
 * whose division boundary falls on this tick, then advance the tick. */
static void clock_tick(void)
{
	int i;
	if (!clk.running)
		return;
	for (i = 0; i < nchannels; i++)
		if (chans[i].type == T_CLOCK
		    && clk.tick % (uint32_t)chans[i].param == 0)
			pulse_left[i] = pulse_samples[i];
	clk.tick++;
}

/* Stop (0xFC): halt and drop every clock output low. */
static void clock_stop(void)
{
	int i;
	clk.running = 0;
	for (i = 0; i < nchannels; i++)
		if (chans[i].type == T_CLOCK)
			pulse_left[i] = 0;
}

/* ------------------------------------------------------------ public API */

void engine_init(const channel *c, int n, uint32_t sample_rate)
{
	int i;
	if (n > MAX_CHANNELS)
		n = MAX_CHANNELS;
	nchannels = n;
	for (i = 0; i < n; i++) {
		chans[i] = c[i];
		pulse_left[i] = 0;
		if (chans[i].type == T_CLOCK) {
			pulse_samples[i] =
				(int)(chans[i].pulse_ms * sample_rate / 1000.0f);
			if (pulse_samples[i] < 1)
				pulse_samples[i] = 1;
		} else {
			pulse_samples[i] = 0;
		}
	}
	memset(state, 0, sizeof state);
	clk.running = 1;
	clk.tick = 0;
}

void engine_run(const midi_ev *evs, int nev, float *outs[], uint32_t nframes)
{
	uint32_t cursor = 0;
	int ev;

	/* Walk events in time order; fill each output up to the event's frame
	 * with pre-event state, then apply it. Transitions land sample-exact. */
	for (ev = 0; ev < nev; ev++) {
		const midi_ev *e = &evs[ev];
		uint8_t status, ch;
		if (e->time > cursor) {
			fill_segment(outs, cursor, e->time);
			cursor = e->time;
		}
		if (e->len == 1) {               /* system real-time, 1 byte */
			switch (e->data[0]) {
			case 0xf8: clock_tick(); break;          /* timing clock */
			case 0xfa: clk.tick = 0; clk.running = 1; break; /* start */
			case 0xfb: clk.running = 1; break;       /* continue */
			case 0xfc: clock_stop(); break;          /* stop */
			}
			continue;
		}
		if (e->len < 3)                  /* handled channel msgs are 3 bytes */
			continue;
		status = e->data[0] & 0xf0;
		ch     = e->data[0] & 0x0f;
		switch (status) {
		case 0x90:                       /* note on */
			if (e->data[2] > 0)
				note_on(&state[ch], e->data[1], e->data[2]);
			else
				note_off(&state[ch], e->data[1]);
			break;
		case 0x80:                       /* note off */
			note_off(&state[ch], e->data[1]);
			break;
		case 0xb0:                       /* control change */
			state[ch].cc[e->data[1]] = e->data[2];
			break;
		}
	}
	fill_segment(outs, cursor, nframes);
}
