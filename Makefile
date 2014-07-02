jackwsmeter: jackwsmeter.c
	gcc -Wall jackwsmeter.c -o jackwsmeter -lwebsockets `pkg-config --cflags --libs jack` -g -lm

all: jackwsmeter
