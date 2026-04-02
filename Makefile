# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -static
LIBS = -lsodium

all: vxclient vxserver generate_keys

vxclient: vxclient.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

vxserver: vxserver.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

generate_keys: generate_keys.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f vxclient vxserver generate_keys
