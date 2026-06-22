/*
 * backend - the seam between the program shell (cli.c) and a backend adapter
 * (jack.c), see CONTEXT.md "backend adapter".
 *
 * cli.c parses argv into a config plus an optional test request and calls
 * backend_run. Everything backend-specific — the audio client, ports, the
 * process callback, and the run loop — lives behind this one function. Porting
 * to ALSA/PipeWire/CoreAudio means writing one new backend_run; cli.c, config,
 * engine, and the tests do not change. No backend types appear here.
 */
#ifndef BACKEND_H
#define BACKEND_H

#include "config.h"

/* --test request: hold one named CV output at a literal sample, ignore MIDI.
 * `active` is 0 for a normal run. `port` (when active) names a cvout in cfg. */
typedef struct {
	const char *port;
	float       value;
	int         active;
} test_req;

/* Set up the backend from `cfg`, run until SIGINT/SIGTERM, then tear down.
 * `test` may be NULL (equivalent to active == 0). Returns a process exit code. */
int backend_run(const config *cfg, const test_req *test);

#endif /* BACKEND_H */
