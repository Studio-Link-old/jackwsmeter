bin_PROGRAMS = jackwsmeter

jackwsmeter_SOURCES = jackwsmeter.c
jackwsmeter_CPPFLAGS = $(JACK_CFLAGS) $(WEBSOCKETS_CFLAGS) -DDATADIR=\"$(datadir)/jackswmeter\"
jackwsmeter_LDADD = $(JACK_LIBS) $(WEBSOCKETS_LIBS) $(LIBM)

htmldir = $(datadir)/jackswmeter
html_DATA = jackwsmeter.html


EXTRA_DIST = $(html_DATA) AUTHORS README
