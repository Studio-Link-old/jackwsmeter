jackwsmeter: jackwsmeter.c
	gcc jackwsmeter.c -o jackwsmeter -lwebsockets `pkg-config --cflags --libs jack` -g -lm

all: jackwsmeter
