#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

int toggle_nonblocking_input()
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
