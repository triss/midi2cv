/*
 * midi2cv - JACK adapter over the MIDI->CV engine (see engine.h, CONTEXT.md).
 *
 * This translation unit is the thin adapter: it parses the config, owns the
 * JACK client and ports, copies JACK MIDI events into portable midi_ev, and
 * hands each block to engine_run. All musical logic lives in the engine.
 *
 * One instance = one config file describing a flat list of output channels.
 *
 * Layout: config parsing -> process callback -> JACK setup -> main.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include <jack/jack.h>
#include <jack/midiport.h>

#include "engine.h"

#define MAX_EVENTS 1024        /* per-block MIDI events copied for the engine */

static channel channels[MAX_CHANNELS];   /* parsed config, passed to the engine */
static int     nchannels;
static char    client_name[64] = "midi2cv";

/* JACK ports, parallel to channels[] — an adapter concern, not the engine's. */
static jack_port_t *ports[MAX_CHANNELS];

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

static int process(jack_nframes_t nframes, void *arg)
{
	void    *mbuf = jack_port_get_buffer(midi_in, nframes);
	float   *outs[MAX_CHANNELS];
	midi_ev  evs[MAX_EVENTS];
	uint32_t ev, count;
	int      i, nev = 0;
	(void)arg;

	for (i = 0; i < nchannels; i++)
		outs[i] = jack_port_get_buffer(ports[i], nframes);

	if (test_mode) {
		for (i = 0; i < nchannels; i++) {
			float s = i == test_chan ? test_value : 0.0f;
			jack_nframes_t f;
			for (f = 0; f < nframes; f++)
				outs[i][f] = s;
		}
		return 0;
	}

	/* Copy JACK events into portable midi_ev for the engine. */
	count = jack_midi_get_event_count(mbuf);
	for (ev = 0; ev < count && nev < MAX_EVENTS; ev++) {
		jack_midi_event_t e;
		if (jack_midi_event_get(&e, mbuf, ev))
			continue;
		evs[nev].time = e.time;
		evs[nev].len  = e.size > 3 ? 3 : (uint8_t)e.size;
		memcpy(evs[nev].data, e.buffer, evs[nev].len);
		nev++;
	}
	engine_run(evs, nev, outs, nframes);
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
	engine_init(channels, nchannels, jack_get_sample_rate(client));

	midi_in = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE,
				     JackPortIsInput, 0);
	for (i = 0; i < nchannels; i++)
		ports[i] = jack_port_register(client, channels[i].name,
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
