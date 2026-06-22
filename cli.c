/*
 * cli - the program shell for midi2cv (see backend.h, config.h).
 *
 * Owns argv parsing and the config load, nothing else: it turns the command
 * line into a config plus an optional test request and hands both to
 * backend_run. No backend or musical logic lives here, so the same shell drives
 * any backend adapter.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "backend.h"

static void usage(void)
{
	fprintf(stderr,
		"usage: midi2cv <config>\n"
		"       midi2cv --test <config> <port>=<sample>\n");
}

int main(int argc, char **argv)
{
	config   cfg;
	test_req test = {0};
	const char *path;

	if (argc == 4 && !strcmp(argv[1], "--test")) {
		char *eq = strchr(argv[3], '=');
		if (!eq) { usage(); return 1; }
		*eq = '\0';
		test.port   = argv[3];
		test.value  = (float)atof(eq + 1);
		test.active = 1;
		path = argv[2];
	} else if (argc == 2) {
		path = argv[1];
	} else {
		usage();
		return 1;
	}

	if (config_load(path, &cfg))
		return 1;
	if (cfg.ncvouts == 0) {
		fprintf(stderr, "midi2cv: config defines no CV outputs\n");
		return 1;
	}

	return backend_run(&cfg, &test);
}
