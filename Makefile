CFLAGS = -Wall -Wextra -Werror -ggdb3 -O3
LDFLAGS = -lasound -lm -lrt

BINS = metronome tuner

all: $(BINS)

metronome: metronome.o common.o

tuner: LDFLAGS += -lfftw3
tuner: tuner.o common.o

clean:
	rm -f *.o $(BINS)
