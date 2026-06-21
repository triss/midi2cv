/*
 * midi2cv - a minimal JACK client turning MIDI into CV for an Expert Sleepers
 * ES-3 (DC-coupled audio out -> ES-3 -> modular).
 *
 * One instance = one config file describing a flat list of output channels.
 * Each channel is one JACK audio port driven by one configurable MIDI source
 * (pitch / gate / trig / vel / cc) and mapped to a float sample value via two
 * per-channel calibration constants (scale, offset).
 *
 * Layout, top to bottom: types -> MIDI state -> config parsing -> JACK setup
 * -> process callback -> main.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include <jack/jack.h>
#include <jack/midiport.h>

/* ------------------------------------------------------------------ types */

typedef enum { T_PITCH, T_GATE, T_TRIG, T_VEL, T_CC, T_CLOCK } chan_type;

typedef struct {
	char       name[64];   /* JACK port short name */
	chan_type  type;
	int        midich;     /* 0..15 (1-based in the config file) */
	int        param;      /* trig: note; cc: CC; clock: division; else -1 */
	float      scale;
	float      offset;
	float      pulse_ms;   /* clock: pulse width (config) */
	int        pulse_samples; /* clock: pulse width in frames (derived) */
	int        pulse_left; /* clock: frames remaining in current pulse */
	jack_port_t *port;
} channel;

/* Per-MIDI-channel playing state. Fixed size, no allocation at run time. */
typedef struct {
	uint8_t stack[128];    /* held notes, last = highest priority */
	int     n;             /* number of held notes */
	int     last_note;     /* top note, held after release (pitch S&H) */
	uint8_t velocity;      /* velocity of the most recent note-on */
	uint8_t cc[128];       /* last value of every CC */
} mchan;

#define MAX_CHANNELS 64

static channel channels[MAX_CHANNELS];
static int     nchannels;
static char    client_name[64] = "midi2cv";
static mchan   state[16];

/* MIDI-clock transport state (system real-time, no channel). Runs by default
 * so bare clock sources work; Start resets phase, Stop halts, Continue resumes. */
static struct { int running; uint32_t tick; } clk = { 1, 0 };

/* connect requests: <port short name> -> <jack target>, applied at startup */
static struct { char from[64]; char to[128]; } connects[MAX_CHANNELS];
static int nconnects;

/* --test mode: hold one named port at a literal sample, ignore MIDI */
static int   test_mode;
static int   test_chan = -1;
static float test_value;

static jack_client_t *client;
static jack_port_t   *midi_in;
static volatile sig_atomic_t running = 1;

/* ------------------------------------------------------------- MIDI state */

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

/* --------------------------------------------------------- config parsing */

static int parse_type(const char *s, chan_type *out)
{
	if (!strcmp(s, "pitch")) *out = T_PITCH;
	else if (!strcmp(s, "gate")) *out = T_GATE;
	else if (!strcmp(s, "trig")) *out = T_TRIG;
	else if (!strcmp(s, "vel"))  *out = T_VEL;
	else if (!strcmp(s, "cc"))   *out = T_CC;
	else if (!strcmp(s, "clock")) *out = T_CLOCK;
	else return -1;
	return 0;
}

static int find_channel(const char *name)
{
	int i;
	for (i = 0; i < nchannels; i++)
		if (!strcmp(channels[i].name, name))
			return i;
	return -1;
}

static int load_config(const char *path)
{
	char line[256];
	int  lineno = 0;
	FILE *f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "midi2cv: cannot open %s\n", path);
		return -1;
	}
	while (fgets(line, sizeof line, f)) {
		char tok[7][64];
		int  ntok;
		lineno++;
		ntok = sscanf(line, "%63s %63s %63s %63s %63s %63s %63s",
			      tok[0], tok[1], tok[2], tok[3], tok[4], tok[5], tok[6]);
		if (ntok < 1 || tok[0][0] == '#')
			continue;

		if (!strcmp(tok[0], "name") && ntok >= 2) {
			snprintf(client_name, sizeof client_name, "%s", tok[1]);
			continue;
		}
		if (!strcmp(tok[0], "connect") && ntok >= 3) {
			if (nconnects < MAX_CHANNELS) {
				snprintf(connects[nconnects].from,
					 sizeof connects[0].from, "%s", tok[1]);
				snprintf(connects[nconnects].to,
					 sizeof connects[0].to, "%s", tok[2]);
				nconnects++;
			}
			continue;
		}

		/* channel: <port> <type> <midich> <param> <scale> <offset> [pulse_ms]
		 * clock uses midich '-' (system-wide), param = division in clocks,
		 * and the optional pulse_ms field; others ignore pulse_ms. */
		if (ntok < 6) {
			fprintf(stderr, "midi2cv: %s:%d: expected at least 6 fields\n",
				path, lineno);
			goto fail;
		}
		if (nchannels >= MAX_CHANNELS) {
			fprintf(stderr, "midi2cv: too many channels\n");
			goto fail;
		}
		channel *c = &channels[nchannels];
		snprintf(c->name, sizeof c->name, "%s", tok[0]);
		if (parse_type(tok[1], &c->type)) {
			fprintf(stderr, "midi2cv: %s:%d: bad type '%s'\n",
				path, lineno, tok[1]);
			goto fail;
		}
		c->param    = (tok[3][0] == '-') ? -1 : atoi(tok[3]);
		c->scale    = atof(tok[4]);
		c->offset   = atof(tok[5]);
		c->pulse_ms = (ntok >= 7) ? atof(tok[6]) : 5.0f;

		if (c->type == T_CLOCK) {
			c->midich = 0;                  /* clock has no channel */
			if (c->param < 1) {
				fprintf(stderr, "midi2cv: %s:%d: clock division "
					"must be >= 1\n", path, lineno);
				goto fail;
			}
		} else {
			c->midich = atoi(tok[2]) - 1;
			if (c->midich < 0 || c->midich > 15) {
				fprintf(stderr, "midi2cv: %s:%d: midi channel "
					"1..16\n", path, lineno);
				goto fail;
			}
		}
		nchannels++;
	}
	fclose(f);
	return 0;
fail:
	fclose(f);
	return -1;
}

/* ------------------------------------------------------- process callback */

/* Write every output's current sample into frames [from, to). */
static void fill_segment(jack_default_audio_sample_t *outs[],
			 jack_nframes_t from, jack_nframes_t to)
{
	int i;
	jack_nframes_t f;
	for (i = 0; i < nchannels; i++) {
		channel *c = &channels[i];
		if (c->type == T_CLOCK) {       /* per-sample pulse countdown */
			float hi = c->offset + c->scale, lo = c->offset;
			for (f = from; f < to; f++)
				outs[i][f] = c->pulse_left > 0
					? (c->pulse_left--, hi) : lo;
		} else {
			float s = channel_sample(c);
			for (f = from; f < to; f++)
				outs[i][f] = s;
		}
	}
}

/* One MIDI timing clock (0xF8): start a fresh pulse on every clock channel
 * whose division boundary falls on this tick, then advance the tick. */
static void clock_tick(void)
{
	int i;
	if (!clk.running)
		return;
	for (i = 0; i < nchannels; i++)
		if (channels[i].type == T_CLOCK
		    && clk.tick % (uint32_t)channels[i].param == 0)
			channels[i].pulse_left = channels[i].pulse_samples;
	clk.tick++;
}

/* Stop (0xFC): halt and drop every clock output low. */
static void clock_stop(void)
{
	int i;
	clk.running = 0;
	for (i = 0; i < nchannels; i++)
		if (channels[i].type == T_CLOCK)
			channels[i].pulse_left = 0;
}

static int process(jack_nframes_t nframes, void *arg)
{
	void *mbuf = jack_port_get_buffer(midi_in, nframes);
	jack_default_audio_sample_t *outs[MAX_CHANNELS];
	jack_nframes_t cursor = 0;
	uint32_t ev, nevents;
	int i;
	(void)arg;

	for (i = 0; i < nchannels; i++)
		outs[i] = jack_port_get_buffer(channels[i].port, nframes);

	if (test_mode) {
		for (i = 0; i < nchannels; i++) {
			float s = i == test_chan ? test_value : 0.0f;
			jack_nframes_t f;
			for (f = 0; f < nframes; f++)
				outs[i][f] = s;
		}
		return 0;
	}

	/* Walk events in time order; fill each output up to the event's frame
	 * with pre-event state, then apply it. Transitions land sample-exact. */
	nevents = jack_midi_get_event_count(mbuf);
	for (ev = 0; ev < nevents; ev++) {
		jack_midi_event_t e;
		uint8_t status, ch;
		if (jack_midi_event_get(&e, mbuf, ev))
			continue;
		if (e.time > cursor) {
			fill_segment(outs, cursor, e.time);
			cursor = e.time;
		}
		if (e.size == 1) {               /* system real-time, 1 byte */
			switch (e.buffer[0]) {
			case 0xf8: clock_tick(); break;          /* timing clock */
			case 0xfa: clk.tick = 0; clk.running = 1; break; /* start */
			case 0xfb: clk.running = 1; break;       /* continue */
			case 0xfc: clock_stop(); break;          /* stop */
			}
			continue;
		}
		if (e.size < 3)                  /* handled channel msgs are 3 bytes */
			continue;
		status = e.buffer[0] & 0xf0;
		ch     = e.buffer[0] & 0x0f;
		switch (status) {
		case 0x90:                       /* note on */
			if (e.buffer[2] > 0)
				note_on(&state[ch], e.buffer[1], e.buffer[2]);
			else
				note_off(&state[ch], e.buffer[1]);
			break;
		case 0x80:                       /* note off */
			note_off(&state[ch], e.buffer[1]);
			break;
		case 0xb0:                       /* control change */
			state[ch].cc[e.buffer[1]] = e.buffer[2];
			break;
		}
	}
	fill_segment(outs, cursor, nframes);
	return 0;
}

/* ----------------------------------------------------------- JACK + setup */

static void on_signal(int sig) { (void)sig; running = 0; }

static int setup_jack(void)
{
	int i;

	client = jack_client_open(client_name, JackNoStartServer, NULL);
	if (!client) {
		fprintf(stderr, "midi2cv: could not connect to JACK\n");
		return -1;
	}
	jack_set_process_callback(client, process, NULL);

	jack_nframes_t sr = jack_get_sample_rate(client);
	for (i = 0; i < nchannels; i++)
		if (channels[i].type == T_CLOCK) {
			channels[i].pulse_samples =
				(int)(channels[i].pulse_ms * sr / 1000.0f);
			if (channels[i].pulse_samples < 1)
				channels[i].pulse_samples = 1;
		}

	midi_in = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE,
				     JackPortIsInput, 0);
	for (i = 0; i < nchannels; i++)
		channels[i].port = jack_port_register(client, channels[i].name,
			JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	if (jack_activate(client)) {
		fprintf(stderr, "midi2cv: cannot activate client\n");
		return -1;
	}

	for (i = 0; i < nconnects; i++) {
		int ci = find_channel(connects[i].from);
		char src[128];
		if (ci < 0)
			continue;
		snprintf(src, sizeof src, "%s:%s", client_name, connects[i].from);
		if (jack_connect(client, src, connects[i].to))
			fprintf(stderr, "midi2cv: connect %s -> %s failed\n",
				src, connects[i].to);
	}
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: midi2cv <config>\n"
		"       midi2cv --test <config> <port>=<sample>\n");
}

int main(int argc, char **argv)
{
	const char *cfg;

	if (argc == 4 && !strcmp(argv[1], "--test")) {
		char *eq;
		cfg = argv[2];
		if (load_config(cfg))
			return 1;
		eq = strchr(argv[3], '=');
		if (!eq) { usage(); return 1; }
		*eq = '\0';
		test_chan = find_channel(argv[3]);
		if (test_chan < 0) {
			fprintf(stderr, "midi2cv: no such port '%s'\n", argv[3]);
			return 1;
		}
		test_value = atof(eq + 1);
		test_mode  = 1;
		fprintf(stderr, "midi2cv: holding %s at %g\n",
			argv[3], test_value);
	} else if (argc == 2) {
		cfg = argv[1];
		if (load_config(cfg))
			return 1;
	} else {
		usage();
		return 1;
	}

	if (nchannels == 0) {
		fprintf(stderr, "midi2cv: config defines no channels\n");
		return 1;
	}
	if (setup_jack())
		return 1;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	fprintf(stderr, "midi2cv: '%s' running with %d channels (Ctrl-C to quit)\n",
		client_name, nchannels);
	while (running)
		sleep(1);

	jack_client_close(client);
	return 0;
}
