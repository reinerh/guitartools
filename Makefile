CFLAGS = -Wall -Wextra -Werror -ggdb3 -O3
LDFLAGS = -lasound -lm -lrt

METRONOME_OBJS = metronome.o
METRONOME_BIN = metronome
TUNER_OBJS = tuner.o
TUNER_BIN = tuner

OBJS = $(METRONOME_OBJS) $(TUNER_OBJS)
BINS = $(METRONOME_BIN) $(TUNER_BIN)

all: $(BINS)

$(METRONOME_BIN): $(METRONOME_OBJS)
$(TUNER_BIN): $(TUNER_OBJS)

clean:
	rm -f $(OBJS) $(BINS)
