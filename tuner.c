#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <complex.h>
#include <fftw3.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 8000
#define FFT_SIZE (1<<13)

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

static void process_frames()
{
	int i, freq, index = -1;
	double largest = -1;

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
	freq = index * SAMPLE_RATE / FFT_SIZE;
	printf("freq: %d Hz\n", freq);
}

static void capture(snd_pcm_t *pcm_handle)
{
	int read;

	while(capturing) {
		while ((read = snd_pcm_readi(pcm_handle, fft_in, FFT_SIZE)) < 0) {
			snd_pcm_prepare(pcm_handle);
			printf("overrun\n");
		}
		if (read == FFT_SIZE) {
			process_frames();
		}
	}
}

int main()
{
	snd_pcm_t *pcm_handle;

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

	return 0;
}
