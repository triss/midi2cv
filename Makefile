CFLAGS = -O2 -Wall -Wextra
LDLIBS = -ljack -lm

midi2cv: midi2cv.o engine.o

midi2cv.o: midi2cv.c engine.h
engine.o: engine.c engine.h

# Engine tests build against engine.c alone — no JACK, no server.
test: engine.c test_engine.c engine.h
	$(CC) $(CFLAGS) engine.c test_engine.c -lm -o /tmp/midi2cv-test && /tmp/midi2cv-test

clean:
	rm -f midi2cv *.o

.PHONY: clean test
