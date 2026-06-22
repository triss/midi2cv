/*
 * config - parse a midi2cv config into a value (see CONTEXT.md "CV output").
 *
 * No JACK dependency: parsing reads a stream and returns a populated struct, so
 * it is tested directly from text (test_config.c) without a file or a server.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

#include "engine.h"

typedef struct {
	char   client_name[64];
	cvout  cvouts[MAX_CVOUTS];
	int    ncvouts;
	struct { char from[64]; char to[128]; } connects[MAX_CVOUTS];
	int     nconnects;
} config;

/* Parse name / connect / CV-output lines from `in` into `out`. Initialises out
 * (client_name defaults to "midi2cv"). Returns 0, or -1 on a malformed line. */
int config_parse(FILE *in, config *out);

/* Open `path` and config_parse it. Returns 0, or -1 on open/parse failure. */
int config_load(const char *path, config *out);

#endif /* CONFIG_H */
