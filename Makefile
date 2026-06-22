CFLAGS = -O2 -Wall -Wextra
LDLIBS = -ljack -lm

midi2cv: midi2cv.o engine.o config.o

midi2cv.o: midi2cv.c engine.h config.h
engine.o: engine.c engine.h
config.o: config.c config.h engine.h

# Unit tests build against the JACK-free modules alone — no server.
test: test-engine test-config

test-engine: engine.c test_engine.c engine.h
	$(CC) $(CFLAGS) engine.c test_engine.c -lm -o /tmp/midi2cv-test-engine && /tmp/midi2cv-test-engine

test-config: config.c test_config.c config.h engine.h
	$(CC) $(CFLAGS) config.c test_config.c -o /tmp/midi2cv-test-config && /tmp/midi2cv-test-config

clean:
	rm -f midi2cv *.o

.PHONY: clean test test-engine test-config
