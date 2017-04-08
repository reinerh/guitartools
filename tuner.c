#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <math.h>
#include <limits.h>
#include <complex.h>
#include <fftw3.h>
#include <alsa/asoundlib.h>

#include "common.h"

#define SAMPLE_RATE 8000
#define FFT_SIZE (1<<13)

#define ACCURACY 1.0 // Hz

static double *fft_in;
static fftw_complex *fft_out;
static fftw_plan fft_plan;

static int capturing = 1;

static void handle_signals(int signum __attribute__((unused)))
{
	capturing = 0;
}

static int set_alsa_params(snd_pcm_t *pcm_handle)
{
	snd_pcm_hw_params_t *hwparams;
	unsigned int rate = SAMPLE_RATE;
	snd_pcm_uframes_t bufsize = FFT_SIZE;

	snd_pcm_hw_params_alloca(&hwparams);

	if (snd_pcm_hw_params_any(pcm_handle, hwparams) < 0) {
		fprintf(stderr, "Failed configuring device\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
		fprintf(stderr, "Failed setting access mode\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_FLOAT64_LE) < 0) {
		fprintf(stderr, "Failed setting format\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &rate, 0) < 0) {
		fprintf(stderr, "Failed setting sample rate\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hwparams, &bufsize) < 0) {
		fprintf(stderr, "Failed setting buffer size\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_channels(pcm_handle, hwparams, 1) < 0) {
		fprintf(stderr, "Failed setting channel number\n");
		return -1;
	}
	if (snd_pcm_hw_params(pcm_handle, hwparams) < 0) {
		fprintf(stderr, "Failed applying hardware parameters\n");
		return -1;
	}

	return 0;
}

static void fft_init()
{
	fft_in = fftw_alloc_real(FFT_SIZE);
	fft_out = fftw_alloc_complex(FFT_SIZE);
	fft_plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in, fft_out, FFTW_ESTIMATE);
}

static void fft_cleanup()
{
	fftw_destroy_plan(fft_plan);
	fftw_free(fft_in);
	fftw_free(fft_out);
}

static void find_note(double freq)
{
	int i, index = 0, dir = 0;
	char note = '?';

	/*                                E,     A,      D,      G,      B,      e                 */
	const double notes[] = { INT_MIN, 82.41, 110.00, 146.83, 196.00, 246.94, 329.63, INT_MAX };
	const char *note_str = "?EADGBe?";

	for (i=0; i<7; i++) {
		if (freq > notes[i+1]) {
			continue;
		}
		if (freq > (notes[i] + notes[i+1])/2) {
			note = note_str[i+1];
			dir = -1;
			index = i+1;
		} else {
			note = note_str[i];
			dir = +1;
			index = i;
		}
		break;
	}

	if (fabs(freq - notes[index]) < ACCURACY) {
		dir = 0;
	}

	printf("\rNote: %c%c%c   Frequency: %.2f Hz      ", dir<0?'>':' ', note, dir>0?'<':' ', freq);
	fflush(stdout);
}

static void process_frames()
{
	int i, index = -1;
	double largest = -1, freq;

	fftw_execute(fft_plan);

	/* find point with largest magnitude */
	for (i=0; i<FFT_SIZE; i++) {
		/* +1 because out[0] is DC result */
		double a = cabs(fft_out[i+1]);
		if (a > largest) {
			largest = a;
			index = i;
		}
	}
	freq = (float)index * SAMPLE_RATE / FFT_SIZE;
	find_note(freq);
}

static void capture(snd_pcm_t *pcm_handle)
{
	while(capturing) {
		int read;
		while ((read = snd_pcm_readi(pcm_handle, fft_in, FFT_SIZE)) < 0) {
			snd_pcm_prepare(pcm_handle);
			printf("overrun\n");
		}
		if (read == FFT_SIZE) {
			process_frames();
		}
	}
	printf("\n");
}

int main()
{
	snd_pcm_t *pcm_handle;

	if (toggle_nonblocking_input() < 0) {
		return 1;
	}

	if (snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
		fprintf(stderr, "Failed opening device for capturing\n");
		return 1;
	}

	if (set_alsa_params(pcm_handle) < 0) {
		return 1;
	}

	signal(SIGINT, handle_signals);
	signal(SIGTERM, handle_signals);

	fft_init();
	capture(pcm_handle);

	snd_pcm_close(pcm_handle);
	fft_cleanup();

	if (toggle_nonblocking_input() < 0) {
		return 1;
	}

	return 0;
}
