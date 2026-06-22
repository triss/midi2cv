/*
 * config - config-file parsing. See config.h and CONTEXT.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

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

int config_parse(FILE *f, config *cfg)
{
	char line[256];
	int  lineno = 0;

	memset(cfg, 0, sizeof *cfg);
	snprintf(cfg->client_name, sizeof cfg->client_name, "midi2cv");

	while (fgets(line, sizeof line, f)) {
		char tok[7][64];
		int  ntok;
		lineno++;
		ntok = sscanf(line, "%63s %63s %63s %63s %63s %63s %63s",
			      tok[0], tok[1], tok[2], tok[3], tok[4], tok[5], tok[6]);
		if (ntok < 1 || tok[0][0] == '#')
			continue;

		if (!strcmp(tok[0], "name") && ntok >= 2) {
			snprintf(cfg->client_name, sizeof cfg->client_name, "%s", tok[1]);
			continue;
		}
		if (!strcmp(tok[0], "connect") && ntok >= 3) {
			if (cfg->nconnects < MAX_CHANNELS) {
				snprintf(cfg->connects[cfg->nconnects].from,
					 sizeof cfg->connects[0].from, "%s", tok[1]);
				snprintf(cfg->connects[cfg->nconnects].to,
					 sizeof cfg->connects[0].to, "%s", tok[2]);
				cfg->nconnects++;
			}
			continue;
		}

		/* channel: <port> <type> <midich> <param> <scale> <offset> [pulse_ms]
		 * clock uses midich '-' (system-wide), param = division in clocks,
		 * and the optional pulse_ms field; others ignore pulse_ms. */
		if (ntok < 6) {
			fprintf(stderr, "midi2cv: line %d: expected at least 6 fields\n",
				lineno);
			return -1;
		}
		if (cfg->nchannels >= MAX_CHANNELS) {
			fprintf(stderr, "midi2cv: too many channels\n");
			return -1;
		}
		channel *c = &cfg->channels[cfg->nchannels];
		snprintf(c->name, sizeof c->name, "%s", tok[0]);
		if (parse_type(tok[1], &c->type)) {
			fprintf(stderr, "midi2cv: line %d: bad type '%s'\n",
				lineno, tok[1]);
			return -1;
		}
		c->param    = (tok[3][0] == '-') ? -1 : atoi(tok[3]);
		c->scale    = atof(tok[4]);
		c->offset   = atof(tok[5]);
		c->pulse_ms = (ntok >= 7) ? atof(tok[6]) : 5.0f;

		if (c->type == T_CLOCK) {
			c->midich = 0;                  /* clock has no channel */
			if (c->param < 1) {
				fprintf(stderr, "midi2cv: line %d: clock division "
					"must be >= 1\n", lineno);
				return -1;
			}
		} else {
			c->midich = atoi(tok[2]) - 1;
			if (c->midich < 0 || c->midich > 15) {
				fprintf(stderr, "midi2cv: line %d: midi channel "
					"1..16\n", lineno);
				return -1;
			}
		}
		cfg->nchannels++;
	}
	return 0;
}

int config_load(const char *path, config *out)
{
	FILE *f = fopen(path, "r");
	int   r;
	if (!f) {
		fprintf(stderr, "midi2cv: cannot open %s\n", path);
		return -1;
	}
	r = config_parse(f, out);
	fclose(f);
	return r;
}
