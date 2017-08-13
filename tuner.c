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
#define NOISE_FLOOR 30
#define MIN_FREQ 10.0 // Hz

#define FFT_INDEX_TO_FREQ(i) ((float)i * SAMPLE_RATE / FFT_SIZE)

static double *fft_in, *magnitudes, *processed;
static fftw_complex *fft_out;
static fftw_plan fft_plan;
static double peak_freq = -1.0;

static int capturing = 1;

struct note {
	const char *name;
	double freq;
};

/* Frequencies taken from Wikipedia
 * https://de.wikipedia.org/wiki/Frequenzen_der_gleichstufigen_Stimmung
 */
struct note note_table[] = {
	{ "---", 00.0000 },
	{ "C0 ", 16.3516 },
	{ "C#0", 17.3239 },
	{ "D0 ", 18.3540 },
	{ "D#0", 19.4454 },
	{ "E0 ", 20.6017 },
	{ "F0 ", 21.8268 },
	{ "F#0", 23.1247 },
	{ "G0 ", 24.4997 },
	{ "G#0", 25.9565 },
	{ "A0 ", 27.5000 },
	{ "A#0", 29.1352 },
	{ "B0 ", 30.8677 },
	{ "C1 ", 32.7032 },
	{ "C#1", 34.6478 },
	{ "D1 ", 36.7081 },
	{ "D#1", 38.8909 },
	{ "E1 ", 41.2034 },
	{ "F1 ", 43.6535 },
	{ "F#1", 46.2493 },
	{ "G1 ", 48.9994 },
	{ "G#1", 51.9131 },
	{ "A1 ", 55.0000 },
	{ "A#1", 58.2705 },
	{ "B1 ", 61.7354 },
	{ "C2 ", 65.4064 },
	{ "C#2", 69.2957 },
	{ "D2 ", 73.4162 },
	{ "D#2", 77.7817 },
	{ "E2 ", 82.4069 },
	{ "F2 ", 87.3071 },
	{ "F#2", 92.4986 },
	{ "G2 ", 97.9989 },
	{ "G#2", 103.826 },
	{ "A2 ", 110.000 },
	{ "A#2", 116.541 },
	{ "B2 ", 123.471 },
	{ "C3 ", 130.813 },
	{ "C#3", 138.591 },
	{ "D3 ", 146.832 },
	{ "D#3", 155.563 },
	{ "E3 ", 164.814 },
	{ "F3 ", 174.614 },
	{ "F#3", 184.997 },
	{ "G3 ", 195.998 },
	{ "G#3", 207.652 },
	{ "A3 ", 220.000 },
	{ "A#3", 233.082 },
	{ "B3 ", 246.942 },
	{ "C4 ", 261.626 },
	{ "C#4", 277.183 },
	{ "D4 ", 293.665 },
	{ "D#4", 311.127 },
	{ "E4 ", 329.628 },
	{ "F4 ", 349.228 },
	{ "F#4", 369.994 },
	{ "G4 ", 391.995 },
	{ "G#4", 415.305 },
	{ "A4 ", 440.000 },
	{ "A#4", 466.164 },
	{ "B4 ", 493.883 },
	{ "C5 ", 523.251 },
	{ "C#5", 554.365 },
	{ "D5 ", 587.330 },
	{ "D#5", 622.254 },
	{ "E5 ", 659.255 },
	{ "F5 ", 698.456 },
	{ "F#5", 739.989 },
	{ "G5 ", 783.991 },
	{ "G#5", 830.609 },
	{ "A5 ", 880.000 },
	{ "A#5", 932.328 },
	{ "B5 ", 987.767 },
	{ "C6 ", 1046.50 },
	{ "C#6", 1108.73 },
	{ "D6 ", 1174.66 },
	{ "D#6", 1244.51 },
	{ "E6 ", 1318.51 },
	{ "F6 ", 1396.91 },
	{ "F#6", 1479.98 },
	{ "G6 ", 1567.98 },
	{ "G#6", 1661.22 },
	{ "A6 ", 1760.00 },
	{ "A#6", 1864.66 },
	{ "B6 ", 1975.53 },
	{ "C7 ", 2093.00 },
	{ "C#7", 2217.46 },
	{ "D7 ", 2349.32 },
	{ "D#7", 2489.02 },
	{ "E7 ", 2637.02 },
	{ "F7 ", 2793.83 },
	{ "F#7", 2959.96 },
	{ "G7 ", 3135.96 },
	{ "G#7", 3322.44 },
	{ "A7 ", 3520.00 },
	{ "A#7", 3729.31 },
	{ "B7 ", 3951.07 },
	{ "C8 ", 4186.01 },
};

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
	magnitudes = fftw_alloc_real(FFT_SIZE);
	processed = fftw_alloc_real(FFT_SIZE);
	fft_in = fftw_alloc_real(FFT_SIZE);
	fft_out = fftw_alloc_complex(FFT_SIZE);
	fft_plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in, fft_out, FFTW_ESTIMATE);
}

static void fft_cleanup()
{
	fftw_destroy_plan(fft_plan);
	fftw_free(magnitudes);
	fftw_free(processed);
	fftw_free(fft_in);
	fftw_free(fft_out);
}

static void update_output(double freq, const char *note, int dir)
{
	char note_str[] = "-----";
	char freq_str[20] = "---";
	char peak_str[20] = "---";

	if (freq >= 0) {
		snprintf(note_str, sizeof(note_str), "%c%s%c", dir<0 ? '>' : ' ', note, dir>0 ? '<' : ' ');
		snprintf(freq_str, sizeof(freq_str), "%.2f Hz", freq);
		snprintf(peak_str, sizeof(peak_str), "%.2f Hz", peak_freq);
	}

	printf("\rNote: %s   Frequency: %s, Peak: %s          ", note_str, freq_str, peak_str);
	fflush(stdout);
}

static void find_note(double freq)
{
	int note = 0, dir = 0;

	if (freq < MIN_FREQ) {
		note = 0;
		dir = 0;
	} else if (freq <= note_table[1].freq) {
		note = 1;
		dir = -1;
	} else if (freq >= note_table[ARRAY_SIZE(note_table)-1].freq) {
		note = ARRAY_SIZE(note_table) - 1;
		dir = +1;
	} else {
		unsigned int i;
		for (i=0; i<ARRAY_SIZE(note_table)-2; i++) {
			double center;
			if (freq > note_table[i+1].freq) {
				continue;
			}
			center = (note_table[i].freq + note_table[i+1].freq) / 2;
			if (freq > center) {
				note = i+1;
				dir = -1;
			} else {
				note = i;
				dir = +1;
			}
			break;
		}
	}

	if (fabs(freq - note_table[note].freq) < ACCURACY) {
		dir = 0;
	}

	update_output(freq, note_table[note].name, dir);
}

static void calculate_magnitudes()
{
	int i, max_i = -1;

	for (i=0; i<FFT_SIZE-1; i++) {
		/* +1 because out[0] is DC result */
		magnitudes[i] = cabs(fft_out[i+1]);

		if (magnitudes[i] < NOISE_FLOOR) {
			magnitudes[i] = 0.0;
		}

		/* also find highest magnitude for later informational output */
		if (magnitudes[i] > magnitudes[max_i]) {
			max_i = i;
		}
	}
	peak_freq = FFT_INDEX_TO_FREQ(max_i);
}

static void apply_hps()
{
	/* Harmonic Product Spectrum
	   enhance magnitude of fundamental frequency by multiplying with harmonics/overtones
	   which can have higher magnitudes than fundamentals */

	int i;

	for (i=0; i<FFT_SIZE; i++) {
		int j;
		processed[i] = magnitudes[i];
		/* next 4 harmonics */
		for (j=2; j<=5; j++) {
			if (i*j >= FFT_SIZE) {
				break;
			}
			if (magnitudes[i*j] < 0.00001) {
				/* too close to zero */
				continue;
			}
			processed[i] *= magnitudes[i*j];
		}
	}
}

static void process_frames()
{
	int i, max_i = -1;
	double freq;

	fftw_execute(fft_plan);
	calculate_magnitudes();
	apply_hps();

	/* find point with largest magnitude */
	for (i=0; i<FFT_SIZE; i++) {
		if (processed[i] > processed[max_i]) {
			max_i = i;
		}
	}
	freq = FFT_INDEX_TO_FREQ(max_i);
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
