/*
 * midi2cv - JACK adapter over the MIDI->CV engine (see engine.h, config.h,
 * CONTEXT.md).
 *
 * This translation unit is the thin adapter: it loads the config, owns the
 * JACK client and ports, copies JACK MIDI events into portable midi_ev, and
 * hands each block to engine_run. All musical logic lives in the engine; all
 * parsing lives in config.
 *
 * One instance = one config file describing a flat list of output channels.
 *
 * Layout: process callback -> JACK setup -> main.
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
#include "config.h"

#define MAX_EVENTS 1024        /* per-block MIDI events copied for the engine */

static config cfg;                       /* parsed config, owned by this adapter */

/* JACK ports, parallel to cfg.channels[] — an adapter concern, not the engine's. */
static jack_port_t *ports[MAX_CHANNELS];

/* --test mode: hold one named port at a literal sample, ignore MIDI */
static int   test_mode;
static int   test_chan = -1;
static float test_value;

static jack_client_t *client;
static jack_port_t   *midi_in;
static volatile sig_atomic_t running = 1;

/* ------------------------------------------------------------ adapter util */

static int find_channel(const char *name)
{
	int i;
	for (i = 0; i < cfg.nchannels; i++)
		if (!strcmp(cfg.channels[i].name, name))
			return i;
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

	for (i = 0; i < cfg.nchannels; i++)
		outs[i] = jack_port_get_buffer(ports[i], nframes);

	if (test_mode) {
		for (i = 0; i < cfg.nchannels; i++) {
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

	client = jack_client_open(cfg.client_name, JackNoStartServer, NULL);
	if (!client) {
		fprintf(stderr, "midi2cv: could not connect to JACK\n");
		return -1;
	}
	jack_set_process_callback(client, process, NULL);
	engine_init(cfg.channels, cfg.nchannels, jack_get_sample_rate(client));

	midi_in = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE,
				     JackPortIsInput, 0);
	for (i = 0; i < cfg.nchannels; i++)
		ports[i] = jack_port_register(client, cfg.channels[i].name,
			JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	if (jack_activate(client)) {
		fprintf(stderr, "midi2cv: cannot activate client\n");
		return -1;
	}

	for (i = 0; i < cfg.nconnects; i++) {
		int ci = find_channel(cfg.connects[i].from);
		char src[128];
		if (ci < 0)
			continue;
		snprintf(src, sizeof src, "%s:%s", cfg.client_name,
			 cfg.connects[i].from);
		if (jack_connect(client, src, cfg.connects[i].to))
			fprintf(stderr, "midi2cv: connect %s -> %s failed\n",
				src, cfg.connects[i].to);
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
	const char *path;

	if (argc == 4 && !strcmp(argv[1], "--test")) {
		char *eq;
		path = argv[2];
		if (config_load(path, &cfg))
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
		path = argv[1];
		if (config_load(path, &cfg))
			return 1;
	} else {
		usage();
		return 1;
	}

	if (cfg.nchannels == 0) {
		fprintf(stderr, "midi2cv: config defines no channels\n");
		return 1;
	}
	if (setup_jack())
		return 1;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	fprintf(stderr, "midi2cv: '%s' running with %d channels (Ctrl-C to quit)\n",
		cfg.client_name, cfg.nchannels);
	while (running)
		sleep(1);

	jack_client_close(client);
	return 0;
}
