/*
 * jackwsmeter - jack meter over websockets
 *
 * Copyright (C) 2014 Frederic Peters <fpeters@0d.be>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see
 * <http://www.gnu.org/licenses/>.
 */

/*
 * based on code,
 * from the libwebsockets test server: LGPL 2.1
 *   Copyright (C) 2010-2011 Andy Green <andy@warmcat.com>
 * from jackmeter, GPL 2+
 *   Copyright (C) 2005  Nicholas J. Humfrey
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <syslog.h>
#include <math.h>

#include <signal.h>

#include <libwebsockets.h>

#include <jack/jack.h>

int max_poll_elements;

struct pollfd *pollfds;
int *fd_lookup;
int count_pollfds;
int num_meters = 0;

int force_exit = 0;

#define MAX_METERS 10

float bias = 1.0f;
float peaks[MAX_METERS];
float sent_peaks[MAX_METERS];

jack_port_t *input_ports[MAX_METERS];
jack_client_t *client = NULL;


/* Read and reset the recent peak sample */
static void read_peaks()
{
	memcpy(sent_peaks, peaks, sizeof(peaks));
	memset(peaks, 0, sizeof(peaks));
}


/* this protocol server (always the first one) just knows how to do HTTP */

static int callback_http(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	int m;
	int fd = (int)(long)user;

	switch (reason) {
	case LWS_CALLBACK_HTTP:
		if (libwebsockets_serve_http_file(context, wsi, "jackwsmeter.html", "text/html"))
			return 1; /* through completion or error, close the socket */

		break;

	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		return 1;

	case LWS_CALLBACK_ADD_POLL_FD:

		if (count_pollfds >= max_poll_elements) {
			lwsl_err("LWS_CALLBACK_ADD_POLL_FD: too many sockets to track\n");
			return 1;
		}

		fd_lookup[fd] = count_pollfds;
		pollfds[count_pollfds].fd = fd;
		pollfds[count_pollfds].events = (int)(long)len;
		pollfds[count_pollfds++].revents = 0;
		break;

	case LWS_CALLBACK_DEL_POLL_FD:
		if (!--count_pollfds)
			break;
		m = fd_lookup[fd];
		/* have the last guy take up the vacant slot */
		pollfds[m] = pollfds[count_pollfds];
		fd_lookup[pollfds[count_pollfds].fd] = m;
		break;

	case LWS_CALLBACK_SET_MODE_POLL_FD:
		pollfds[fd_lookup[fd]].events |= (int)(long)len;
		break;

	case LWS_CALLBACK_CLEAR_MODE_POLL_FD:
		pollfds[fd_lookup[fd]].events &= ~(int)(long)len;
		break;

	default:
		break;
	}

	return 0;
}

static int
callback_meter(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason,
		void *user, void *in, size_t len)
{
	int n;
	int i;
	float db;
	char one_peak[100];
	char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 + LWS_SEND_BUFFER_POST_PADDING];
	char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		p[0] = '\0';
		for (i=0; i<num_meters; i++) {
			db = 20.0f * log10f(sent_peaks[i] * bias);
			snprintf(one_peak, 100, "%f ", db);
			strcat((char*)p, one_peak);
		}
		n = strlen(p) + 1;
		n = libwebsocket_write(wsi, (unsigned char*)p, n, LWS_WRITE_TEXT);
		if (n < 0) {
			lwsl_err("ERROR %d writing to socket\n", n);
			return 1;
		}
		break;

	default:
		break;
	}

	return 0;
}

/* list of supported protocols and callbacks */

static struct libwebsocket_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"http-only",		/* name */
		callback_http,		/* callback */
		0,			/* per_session_data_size */
		0,			/* max frame size / rx buffer */
	},
	{
		"jack-wsmeter-protocol",
		callback_meter,
		0,
		0,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};

void sighandler(int sig)
{
	force_exit = 1;
}

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "port",	required_argument,	NULL, 'p' },
	{ "ssl",	no_argument,		NULL, 's' },
	{ "interface",  required_argument,	NULL, 'i' },
	{ "closetest",  no_argument,		NULL, 'c' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", 	no_argument,		NULL, 'D' },
#endif
	{ "name",	required_argument,	NULL, 'n' },
	{ NULL, 0, 0, 0 }
};


/* Callback called by JACK when audio is available.
   Stores value of peak sample */
static int process_peak(jack_nframes_t nframes, void *arg)
{
	unsigned int i, port;

	for (port = 0; port < num_meters; port++) {
		jack_default_audio_sample_t *in;

		/* just incase the port isn't registered yet */
		if (input_ports[port] == 0) {
			break;
		}

		in = (jack_default_audio_sample_t *) jack_port_get_buffer(input_ports[port], nframes);

		for (i = 0; i < nframes; i++) {
			const float s = fabs(in[i]);
			if (s > peaks[port]) {
				peaks[port] = s;
			}
		}
	}

	return 0;
}


/* Close down JACK when exiting */
static void cleanup()
{
	const char **all_ports;
	unsigned int i, j;

	lwsl_debug("cleanup()\n");

	for (i=0; i<num_meters; i++) {
		all_ports = jack_port_get_all_connections(client, input_ports[i]);

		for (j=0; all_ports && all_ports[j]; j++) {
			jack_disconnect(client, all_ports[j], jack_port_name(input_ports[i]));
		}
	}

	/* Leave the jack graph */
	jack_client_close(client);

	closelog();
}


int main(int argc, char **argv)
{
	int n = 0;
	int use_ssl = 0;
	struct libwebsocket_context *context;
	int opts = 0;
	char interface_name[128] = "";
	char jack_name[128] = "wsmeter";
	const char *iface = NULL;
#ifndef WIN32
	int syslog_options = LOG_PID | LOG_PERROR;
#endif
	unsigned int oldus = 0;
	struct lws_context_creation_info info;

	int debug_level = 7;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	jack_status_t status;

	memset(&info, 0, sizeof info);
	info.port = 7681;

	while (n >= 0) {
		n = getopt_long(argc, argv, "ci:hsp:d:Dn:", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			syslog_options &= ~LOG_PERROR;
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 's':
			use_ssl = 1;
			break;
		case 'p':
			info.port = atoi(optarg);
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			iface = interface_name;
			break;
		case 'n':
			strncpy(jack_name, optarg, sizeof jack_name);
			jack_name[(sizeof jack_name) - 1] = '\0';
			break;
		case 'h':
			fprintf(stderr, "Usage: jackwsserver "
					"[--port=<p>] [--ssl] "
					"[-d <log bitfield>] <port>+\n");
			exit(1);
		}
	}

#if !defined(LWS_NO_DAEMONIZE)
	/* 
	 * normally lock path would be /var/lock/jwsm or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.jwsm-lock
	 */
	if (daemonize && lws_daemonize("/tmp/.jwsm-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif

	signal(SIGINT, sighandler);

	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("jackwsmeter", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	max_poll_elements = getdtablesize();
	pollfds = malloc(max_poll_elements * sizeof (struct pollfd));
	fd_lookup = malloc(max_poll_elements * sizeof (int));
	if (pollfds == NULL || fd_lookup == NULL) {
		lwsl_err("Out of memory pollfds=%d\n", max_poll_elements);
		return -1;
	}

	info.iface = iface;
	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = libwebsocket_get_internal_extensions();
#endif
	if (!use_ssl) {
		info.ssl_cert_filepath = NULL;
		info.ssl_private_key_filepath = NULL;
	} else {
		/*
		info.ssl_cert_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.pem";
		info.ssl_private_key_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.key.pem";
		*/
	}
	info.gid = -1;
	info.uid = -1;
	info.options = opts;

	// Register with Jack
	if ((client = jack_client_open(jack_name, JackNullOption, &status)) == 0) {
		lwsl_err("Failed to start jack client: %d\n", status);
		exit(1);
	}
	lwsl_debug("Registering as '%s'.\n", jack_get_client_name(client));

	// Register the cleanup function to be called when program exits
	atexit(cleanup);

	// Register the peak signal callback
	jack_set_process_callback(client, process_peak, 0);


	if (jack_activate(client)) {
		lwsl_err("Cannot activate client.\n");
		exit(1);
	}

	opts = optind;
	num_meters = 0;
	while (argv[opts]) {
		char in_name[255];
		jack_port_t *port;
		// Create our input port
		snprintf(in_name, 255, "in_%d", num_meters);
		if (!(input_ports[num_meters] = jack_port_register(client, in_name,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			lwsl_err("Cannot register input port '%s'.\n", in_name);
			exit(1);
		}

		port = jack_port_by_name(client, argv[opts]);
		if (port == NULL) {
			lwsl_err("Can't find port '%s'\n", argv[opts]);
		} else {
			if (jack_connect(client, jack_port_name(port), jack_port_name(input_ports[num_meters]))) {
				lwsl_err("failed to connect to port '%s'\n", argv[opts]);
				exit(1);
			}
		}
		num_meters += 1;
		opts++;
	}

	if (num_meters == 0) {
		lwsl_err("You must specify at least one port, aborting.");
		exit(1);
	}

	context = libwebsocket_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	n = 0;
	while (n >= 0 && !force_exit) {
		struct timeval tv;

		gettimeofday(&tv, NULL);

		read_peaks();

		/*
		 * This provokes the LWS_CALLBACK_SERVER_WRITEABLE for every
		 * live websocket connection as soon as it can take more packets
		 * (usually immediately)
		 */

		if (((unsigned int)tv.tv_usec - oldus) > 100000) {
			libwebsocket_callback_on_writable_all_protocol(&protocols[1]);
			oldus = tv.tv_usec;
		}

		n = poll(pollfds, count_pollfds, 25);
		if (n < 0)
			continue;


		if (n)
			for (n = 0; n < count_pollfds; n++)
				if (pollfds[n].revents)
					if (libwebsocket_service_fd(context,
								  &pollfds[n]) < 0)
						goto done;


		n = libwebsocket_service(context, 25);
	}

done:
	libwebsocket_context_destroy(context);

	lwsl_notice("jackwsserver exited cleanly\n");

	return 0;
}
