CFLAGS ?= -O2 -Wall -Wextra

all: diagnostic

diagnostic: diagnostic.c
	$(CC) $(CFLAGS) -o $@ $< -lusb-1.0 -lpthread

clean:
	rm -f diagnostic

.PHONY: all clean
