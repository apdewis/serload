CC = gcc
CFLAGS = -I ../argparse 
LDFLAGS = -L ../argparse -lc -lm -largparse -static

serload: loader.c 
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)
