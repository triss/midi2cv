/*
 * test_engine - direct unit tests for the MIDI->CV engine, no JACK.
 *
 * Builds against engine.c alone (`make test`). The checks here are the ones
 * that previously needed a running server plus audio capture.
 */
#include <stdio.h>
#include <math.h>

#include "engine.h"

static int fails;

#define CHECK(c) do { if (!(c)) { \
	printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)

#define SR 48000

/* midi_ev constructors */
static midi_ev mev_note_on(uint32_t t, int ch, int note, int vel)
{ midi_ev e = { t, { (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)vel }, 3 }; return e; }
static midi_ev mev_note_off(uint32_t t, int ch, int note)
{ midi_ev e = { t, { (uint8_t)(0x80 | ch), (uint8_t)note, 0 }, 3 }; return e; }
static midi_ev mev_cc(uint32_t t, int ch, int num, int val)
{ midi_ev e = { t, { (uint8_t)(0xb0 | ch), (uint8_t)num, (uint8_t)val }, 3 }; return e; }
static midi_ev mev_rt(uint32_t t, uint8_t status)
{ midi_ev e = { t, { status, 0, 0 }, 1 }; return e; }

/* pitch tracks the held note, and the transition lands sample-exact */
static void test_pitch_sample_accurate(void)
{
	cvout ch = { "p", S_PITCH, 0, -1, 0.008333f, 0.0f, 0 };
	float b[64]; float *outs[1] = { b };
	midi_ev evs[1] = { mev_note_on(32, 0, 60, 100) };

	engine_init(&ch, 1, SR);
	engine_run(evs, 1, outs, 64);
	CHECK(NEAR(b[31], 0.0f));            /* before the note: last_note 0 */
	CHECK(NEAR(b[32], 60 * 0.008333f));  /* edge lands exactly at frame 32 */
	CHECK(NEAR(b[63], 0.49998f));
}

/* gate is high while a note is held, low after release; state persists */
static void test_gate_hold_release(void)
{
	cvout ch = { "g", S_GATE, 0, -1, 0.5f, 0.0f, 0 };
	float b[16]; float *outs[1] = { b };
	midi_ev on = mev_note_on(0, 0, 64, 100);
	midi_ev off = mev_note_off(0, 0, 64);

	engine_init(&ch, 1, SR);
	engine_run(&on, 1, outs, 16);
	CHECK(NEAR(b[15], 0.5f));
	engine_run(&off, 1, outs, 16);       /* next block, same engine state */
	CHECK(NEAR(b[15], 0.0f));
}

/* trig fires only for its bound note */
static void test_trig_specific_note(void)
{
	cvout ch = { "t", S_TRIG, 0, 36, 0.5f, 0.0f, 0 };
	float b[8]; float *outs[1] = { b };
	midi_ev other = mev_note_on(0, 0, 38, 100);
	midi_ev mine  = mev_note_on(0, 0, 36, 100);

	engine_init(&ch, 1, SR);
	engine_run(&other, 1, outs, 8);
	CHECK(NEAR(b[7], 0.0f));              /* wrong note: stays low */
	engine_run(&mine, 1, outs, 8);
	CHECK(NEAR(b[7], 0.5f));              /* bound note: fires */
}

/* vel and cc scale 0..127 -> 0..1 */
static void test_vel_and_cc(void)
{
	cvout chv = { "v", S_VEL, 0, -1, 1.0f, 0.0f, 0 };
	cvout chc = { "c", S_CC, 0, 1, 1.0f, 0.0f, 0 };
	float b[4]; float *outs[1] = { b };
	midi_ev von = mev_note_on(0, 0, 60, 64);
	midi_ev cc  = mev_cc(0, 0, 1, 127);

	engine_init(&chv, 1, SR);
	engine_run(&von, 1, outs, 4);
	CHECK(NEAR(b[3], 64 / 127.0f));
	engine_init(&chc, 1, SR);
	engine_run(&cc, 1, outs, 4);
	CHECK(NEAR(b[3], 1.0f));
}

/* clock divides, runs without a Start, and pulses are pulse_samples wide */
static void test_clock_division_and_width(void)
{
	/* pulse_ms 0.1 @ 48k -> 4 frames; div 2 */
	cvout ch = { "clk", S_CLOCK, 0, 2, 0.5f, 0.0f, 0.1f };
	float b[64]; float *outs[1] = { b };
	midi_ev evs[3] = { mev_rt(0, 0xf8), mev_rt(10, 0xf8), mev_rt(20, 0xf8) };

	engine_init(&ch, 1, SR);
	engine_run(evs, 3, outs, 64);
	CHECK(NEAR(b[0], 0.5f));  CHECK(NEAR(b[3], 0.5f));  /* tick0 pulse, 4 wide */
	CHECK(NEAR(b[4], 0.0f));
	CHECK(NEAR(b[10], 0.0f));                           /* tick1: no pulse */
	CHECK(NEAR(b[20], 0.5f)); CHECK(NEAR(b[23], 0.5f)); /* tick2 pulse */
	CHECK(NEAR(b[24], 0.0f));
}

/* Stop cuts the pulse and halts; later clocks are ignored */
static void test_clock_stop(void)
{
	cvout ch = { "clk", S_CLOCK, 0, 1, 0.5f, 0.0f, 0.1f };  /* 4-frame pulse */
	float b[8]; float *outs[1] = { b };
	midi_ev evs[2] = { mev_rt(0, 0xf8), mev_rt(2, 0xfc) };

	engine_init(&ch, 1, SR);
	engine_run(evs, 2, outs, 8);
	CHECK(NEAR(b[0], 0.5f)); CHECK(NEAR(b[1], 0.5f));  /* pulse started */
	CHECK(NEAR(b[2], 0.0f)); CHECK(NEAR(b[7], 0.0f));  /* Stop cut it low */
}

/* Start resets the divider phase */
static void test_clock_start_resets_phase(void)
{
	cvout ch = { "clk", S_CLOCK, 0, 4, 0.5f, 0.0f, 0.04f }; /* ~2-frame pulse */
	float b[16]; float *outs[1] = { b };
	midi_ev evs[5] = {
		mev_rt(0, 0xf8),   /* tick0 -> fires */
		mev_rt(1, 0xf8),   /* tick1 */
		mev_rt(2, 0xf8),   /* tick2 */
		mev_rt(3, 0xfa),   /* Start: tick -> 0 */
		mev_rt(4, 0xf8),   /* tick0 again -> fires */
	};

	engine_init(&ch, 1, SR);
	engine_run(evs, 5, outs, 16);
	CHECK(NEAR(b[0], 0.5f));   /* initial downbeat */
	CHECK(NEAR(b[2], 0.0f));   /* tick2: no fire (div 4) */
	CHECK(NEAR(b[3], 0.0f));
	CHECK(NEAR(b[4], 0.5f));   /* fires again because Start reset the phase */
}

int main(void)
{
	test_pitch_sample_accurate();
	test_gate_hold_release();
	test_trig_specific_note();
	test_vel_and_cc();
	test_clock_division_and_width();
	test_clock_stop();
	test_clock_start_resets_phase();

	if (fails) {
		printf("\n%d check(s) FAILED\n", fails);
		return 1;
	}
	printf("all engine tests passed\n");
	return 0;
}
