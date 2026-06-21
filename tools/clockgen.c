/* throwaway JACK MIDI clock generator: emits Start then 0xF8 every PERIOD
 * frames. Usage: clockgen <period_frames> */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>

static jack_client_t *c;
static jack_port_t *out;
static jack_nframes_t period = 1000;
static jack_nframes_t acc;      /* frames since last clock */
static int started;

static int process(jack_nframes_t n, void *arg)
{
	void *buf = jack_port_get_buffer(out, n);
	jack_nframes_t i;
	(void)arg;
	jack_midi_clear_buffer(buf);
	if (!started) {              /* one Start at frame 0 */
		unsigned char s = 0xFA;
		jack_midi_event_write(buf, 0, &s, 1);
		started = 1;
	}
	for (i = 0; i < n; i++) {
		if (acc == 0) {
			unsigned char s = 0xF8;
			jack_midi_event_write(buf, i, &s, 1);
		}
		if (++acc >= period)
			acc = 0;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1) period = atoi(argv[1]);
	c = jack_client_open("clockgen", JackNoStartServer, NULL);
	jack_set_process_callback(c, process, NULL);
	out = jack_port_register(c, "out", JACK_DEFAULT_MIDI_TYPE,
				 JackPortIsOutput, 0);
	jack_activate(c);
	for (;;) sleep(1);
	return 0;
}
