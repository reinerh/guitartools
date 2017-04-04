#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 8000

#define BUFFER_SIZE 512

/* 2 bytes per sample; 0.1 seconds long */
#define TONE_SIZE (2*SAMPLE_RATE/10)

#define TONE1_FREQ 800
#define TONE2_FREQ 440

static uint8_t tone1[TONE_SIZE];
static uint8_t tone2[TONE_SIZE];
static const uint8_t silence[BUFFER_SIZE];

static int playing = 1;
static int bpm = 120;
static const char *pattern = "1222";

static void handle_signals(int signum __attribute__((unused)))
{
	playing = 0;
}

static void instructions()
{
	printf("\rPress: (q)uit, (+) faster, (-) slower.  State: %d bpm    ", bpm);
	fflush(stdout);
}

static int set_alsa_params(snd_pcm_t *pcm_handle)
{
	snd_pcm_hw_params_t *hwparams;
	unsigned int rate = SAMPLE_RATE;
	snd_pcm_uframes_t bufsize = BUFFER_SIZE;

	snd_pcm_hw_params_alloca(&hwparams);

	if (snd_pcm_hw_params_any(pcm_handle, hwparams) < 0) {
		fprintf(stderr, "Failed configuring device\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
		fprintf(stderr, "Failed setting access mode\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_S16_LE) < 0) {
		fprintf(stderr, "Failed setting format\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &rate, 0) < 0) {
		fprintf(stderr, "Failed setting sample rate\n");
		return -1;
	}
	if (snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hwparams, &bufsize) < 0) {
		fprintf(stderr, "Failed setting sample rate\n");
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

static int toggle_nonblocking_input()
{
	static struct termios *saved_termios = NULL;
	int ret = 0;

	if (saved_termios) {
		/* restore old settings */
		if ((ret = tcsetattr(STDIN_FILENO, TCSANOW, saved_termios)) < 0) {
			fprintf(stderr, "Failed restoring termios settings: %s\n", strerror(errno));
		}
		free(saved_termios);
		saved_termios = NULL;

		/* restore cursor */
		fprintf(stderr, "\033[?25h");
	} else {
		struct termios new_termios;

		/* get and backup current settings */
		saved_termios = malloc(sizeof(struct termios));
		if (tcgetattr(STDIN_FILENO, saved_termios) < 0) {
			fprintf(stderr, "Failed retrieving current termios setings: %s\n", strerror(errno));
			free(saved_termios);
			saved_termios = NULL;
			return -1;
		}
		memcpy(&new_termios, saved_termios, sizeof(struct termios));

		/* disable echo and canonical mode (line by line input; line editing) */
		new_termios.c_lflag &= ~(ICANON | ECHO);

		if ((ret = tcsetattr(STDIN_FILENO, TCSANOW, &new_termios)) < 0) {
			fprintf(stderr, "Failed setting to changed termios: %s\n", strerror(errno));
		}

		/* disable cursor */
		fprintf(stderr, "\033[?25l");
	}

	return ret;
}

static int input_available()
{
	struct timeval timeout = {0, 0};
	int count;
	fd_set inputs;

	FD_ZERO(&inputs);
	FD_SET(STDIN_FILENO, &inputs);

	count = select(STDIN_FILENO+1, &inputs, NULL, NULL, &timeout);
	if (count == -1) {
		fprintf(stderr, "Failed checking stdin status: %s\n", strerror(errno));
		return 0;
	}

	return count == 1 && FD_ISSET(STDIN_FILENO, &inputs);
}

static void handle_keypress(char c)
{
	switch(c) {
		case 'q':
			playing = 0;
			break;
		case '+':
			bpm++;
			break;
		case '-':
			bpm--;
			break;
	}

	instructions();
}

static void prepare_tones()
{
	int i, amplitude = 20;

	for(i=0; i<TONE_SIZE-1; i++) {
		uint16_t sample = amplitude * sin(2*M_PI*TONE1_FREQ*i/SAMPLE_RATE);
		tone1[i] = sample % 256;
		tone1[i+1] = sample >> 8;
		sample = amplitude * sin(2*M_PI*TONE2_FREQ*i/SAMPLE_RATE);
		tone2[i] = sample % 256;
		tone2[i+1] = sample >> 8;
	}
}

static long timediff_us(struct timespec *ts1, struct timespec *ts2)
{
	struct timeval tv1, tv2, diff;

	TIMESPEC_TO_TIMEVAL(&tv1, ts1);
	TIMESPEC_TO_TIMEVAL(&tv2, ts2);

	timersub(&tv1, &tv2, &diff);

	return diff.tv_sec * 1000000 + diff.tv_usec;
}

static void play_tone(snd_pcm_t *pcm_handle, char tone)
{
	const uint8_t *tonebuf;
	int tonebuf_size;

	switch(tone) {
		case '1':
			tonebuf = tone1;
			tonebuf_size = sizeof(tone1);
			break;
		case '2':
			tonebuf = tone2;
			tonebuf_size = sizeof(tone2);
			break;
		case 's':
		default:
			tonebuf = silence;
			tonebuf_size = sizeof(silence);
			break;
	}
	while (snd_pcm_writei(pcm_handle, tonebuf, tonebuf_size / 2) < 0) {
		snd_pcm_prepare(pcm_handle);
		printf("underrun\n");
	}
}

static void play(snd_pcm_t *pcm_handle, const char *pattern)
{
	int i = 0;
	struct timespec cur_t, old_t = {};

	/* fill buffer with silence */
	snd_pcm_prepare(pcm_handle);
	play_tone(pcm_handle, 's');

	while(playing) {
		double period_us = 1000000 * 60.0 / bpm;

		if (input_available()) {
			char c;
			read(STDIN_FILENO, &c, 1);
			handle_keypress(c);
		}

		if (clock_gettime(CLOCK_MONOTONIC, &cur_t) == -1) {
			fprintf(stderr, "Failed getting time: %s\n", strerror(errno));
			return;
		}

		if (timediff_us(&cur_t, &old_t) >= period_us) {
			memcpy(&old_t, &cur_t, sizeof(struct timespec));
			play_tone(pcm_handle, pattern[i++ % strlen(pattern)]);
		} else {
			play_tone(pcm_handle, 's');
		}
	}
}

static void usage(const char *name)
{
	printf("Usage: %s [OPTION]\n", name);
	printf("  -b, --bpm=N             Set beats per minute\n");
	printf("  -p, --pattern=PATTERN   Set beeping pattern (e.g. 1222)\n");
}

static int parse_cmdline(int argc, char *argv[])
{
	int opt;
	struct option options[] = {
		{"help", no_argument, 0, 'h'},
		{"bpm", required_argument, 0, 'b'},
		{"pattern", required_argument, 0, 'p'},
		{0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "hb:p:", options, NULL)) != -1) {
		switch(opt) {
			case 'b':
				if (sscanf(optarg, "%d", &bpm) != 1) {
					return -1;
				}
				break;
			case 'p':
				pattern = strdup(optarg);
				break;
			case 'h':
			default:
				return -1;
		}

	}

	return 0;
}

int main(int argc, char *argv[])
{
	snd_pcm_t *pcm_handle;

	if (parse_cmdline(argc, argv) < 0) {
		usage(argv[0]);
		return 1;
	}

	if (toggle_nonblocking_input() < 0) {
		return 1;
	}

	if (snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
		fprintf(stderr, "Failed opening device\n");
		return 1;
	}

	if (set_alsa_params(pcm_handle) < 0) {
		return 1;
	}

	signal(SIGINT, handle_signals);
	signal(SIGTERM, handle_signals);

	instructions();
	prepare_tones();
	play(pcm_handle, pattern);
	printf("\n");

	snd_pcm_close(pcm_handle);

	if (toggle_nonblocking_input() < 0) {
		return 1;
	}

	return 0;
}
