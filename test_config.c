/*
 * test_config - direct unit tests for config parsing, no JACK, no temp files.
 *
 * Feeds config text through fmemopen and asserts on the returned struct.
 * Builds against config.c + engine.h types (`make test`).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>

#include "config.h"

static int fails;

#define CHECK(c) do { if (!(c)) { \
	printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int parse(const char *text, config *cfg)
{
	FILE *f = fmemopen((void *)text, strlen(text), "r");
	int r = config_parse(f, cfg);
	fclose(f);
	return r;
}

/* a full config: name, two source types, a comment, a connect */
static void test_full(void)
{
	config cfg;
	int r = parse(
		"name drumvoice\n"
		"pitch1 pitch 1 - 0.008333 0.0\n"
		"# a comment line\n"
		"kick trig 10 36 0.5 0.0\n"
		"connect pitch1 system:playback_1\n", &cfg);

	CHECK(r == 0);
	CHECK(strcmp(cfg.client_name, "drumvoice") == 0);
	CHECK(cfg.ncvouts == 2);
	CHECK(cfg.cvouts[0].src == S_PITCH);
	CHECK(cfg.cvouts[0].midich == 0);          /* 1-based -> 0 */
	CHECK(cfg.cvouts[1].src == S_TRIG);
	CHECK(cfg.cvouts[1].midich == 9);          /* channel 10 */
	CHECK(cfg.cvouts[1].param == 36);
	CHECK(cfg.nconnects == 1);
	CHECK(strcmp(cfg.connects[0].from, "pitch1") == 0);
	CHECK(strcmp(cfg.connects[0].to, "system:playback_1") == 0);
}

/* clock: '-' midich, optional pulse_ms defaults to 5 */
static void test_clock_defaults(void)
{
	config cfg;
	CHECK(parse("q clock - 24 0.5 0.0\n", &cfg) == 0);
	CHECK(cfg.cvouts[0].src == S_CLOCK);
	CHECK(cfg.cvouts[0].param == 24);
	CHECK(cfg.cvouts[0].pulse_ms == 5.0f);     /* default */
	CHECK(parse("q clock - 24 0.5 0.0 10\n", &cfg) == 0);
	CHECK(cfg.cvouts[0].pulse_ms == 10.0f);    /* explicit */
}

/* client name defaults when no 'name' line */
static void test_default_name(void)
{
	config cfg;
	CHECK(parse("g gate 1 - 0.5 0.0\n", &cfg) == 0);
	CHECK(strcmp(cfg.client_name, "midi2cv") == 0);
}

/* malformed lines are rejected */
static void test_rejects_bad(void)
{
	config cfg;
	CHECK(parse("bogus pitch 1\n", &cfg) == -1);          /* < 6 fields */
	CHECK(parse("g notatype 1 - 0.5 0.0\n", &cfg) == -1); /* bad source */
	CHECK(parse("g gate 99 - 0.5 0.0\n", &cfg) == -1);    /* channel out of range */
	CHECK(parse("q clock - 0 0.5 0.0\n", &cfg) == -1);    /* division < 1 */
}

int main(void)
{
	test_full();
	test_clock_defaults();
	test_default_name();
	test_rejects_bad();

	if (fails) {
		printf("\n%d config check(s) FAILED\n", fails);
		return 1;
	}
	printf("all config tests passed\n");
	return 0;
}
