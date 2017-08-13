CFLAGS = -Wall -Wextra -Werror -ggdb3 -O3
LDFLAGS = -lasound -lm -lrt
PREFIX=/usr/local

BINS = metronome tuner

all: $(BINS)

metronome: metronome.o common.o

tuner: LDFLAGS += -lfftw3
tuner: tuner.o common.o

install: all
	install -vDt $(DESTDIR)$(PREFIX)/bin $(BINS)

uninstall:
	rm -vf $(addprefix $(DESTDIR)$(PREFIX)/bin/, $(BINS))

clean:
	rm -f *.o $(BINS)
