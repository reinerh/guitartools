#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 8000
#define BUFFER_SIZE 4096

static int capturing = 1;

static void handle_signals(int signum __attribute__((unused)))
{
	capturing = 0;
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

static void capture(snd_pcm_t *pcm_handle)
{
	char tonebuf[BUFFER_SIZE];
	int n_frames = 0;
	FILE* f = fopen("capture.raw", "w");

	while(capturing) {
		while ((n_frames = snd_pcm_readi(pcm_handle, tonebuf, sizeof(tonebuf) / 2)) < 0) {
			snd_pcm_prepare(pcm_handle);
			printf("overrun\n");
		}
		fwrite(tonebuf, n_frames*2, 1, f);
	}
	fclose(f);
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

	capture(pcm_handle);

	snd_pcm_close(pcm_handle);

	return 0;
}
