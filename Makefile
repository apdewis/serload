CC = gcc
CFLAGS = -I ../argparse 
LDFLAGS = -L ../argparse -lc -lm -largparse -static

serload: serload.c 
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)
