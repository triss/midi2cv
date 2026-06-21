CFLAGS = -O2 -Wall -Wextra
LDLIBS = -ljack -lm

midi2cv: midi2cv.c

clean:
	rm -f midi2cv

.PHONY: clean
