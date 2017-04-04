CFLAGS = -Wall -Wextra -Werror -ggdb3 -O3
LDFLAGS = -lasound -lm -lrt

OBJS = metronome.o
BIN = metronome

$(BIN): $(OBJS)

all: $(BIN)

clean:
	rm -f $(OBJS) $(BIN)
