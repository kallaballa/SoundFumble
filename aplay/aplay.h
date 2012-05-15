/*
 *  aplay.c - plays and records
 *
 *      CREATIVE LABS CHANNEL-files
 *      Microsoft WAVE-files
 *      SPARC AUDIO .AU-files
 *      Raw Data
 *
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Based on vplay program by Michael Beck
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <alsa/asoundlib.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include "formats.h"

#ifndef LLONG_MAX
#define LLONG_MAX    9223372036854775807LL
#endif

#define DEFAULT_FORMAT		SND_PCM_FORMAT_U8
#define DEFAULT_SPEED 		8000

#define FORMAT_DEFAULT		-1
#define FORMAT_RAW		0
#define FORMAT_VOC		1
#define FORMAT_WAVE		2
#define FORMAT_AU		3

/* global data */

static snd_pcm_sframes_t (*readi_func)(snd_pcm_t *handle, void *buffer, snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writei_func)(snd_pcm_t *handle, const void *buffer, snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*readn_func)(snd_pcm_t *handle, void **bufs, snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writen_func)(snd_pcm_t *handle, void **bufs, snd_pcm_uframes_t size);
static int playback_init(int argc, char *argv[]);
static void playback_open();
static void playback_quit();

static char *command;
static snd_pcm_t *handle;
static struct {
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} hwparams, rhwparams;
static int timelimit = 0;
static int quiet_mode = 0;
static int file_type = FORMAT_DEFAULT;
static unsigned int sleep_min = 0;
static int open_mode = 0;
static snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
static int mmap_flag = 0;
static int interleaved = 1;
static int nonblock = 0;
static char *audiobuf = NULL;
static snd_pcm_uframes_t chunk_size = 0;
static unsigned period_time = 0;
static unsigned buffer_time = 0;
static snd_pcm_uframes_t period_frames = 0;
static snd_pcm_uframes_t buffer_frames = 0;
static int avail_min = -1;
static int start_delay = 0;
static int stop_delay = 0;
static int verbose = 0;
static int buffer_pos = 0;
static size_t bits_per_sample, bits_per_frame;
static size_t chunk_bytes;


static int fd = -1;
static off_t pbrec_count = LONG_MAX;
static off_t fdcount;
static int vocmajor, vocminor;

/* needed prototypes */


#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
#define error(...) do {\
	fprintf(stderr, "%s: %s:%d: ", command, __FUNCTION__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	putc('\n', stderr); \
} while (0)
#else
#define error(args...) do {\
	fprintf(stderr, "%s: %s:%d: ", command, __FUNCTION__, __LINE__); \
	fprintf(stderr, ##args); \
	putc('\n', stderr); \
} while (0)
#endif	

static void usage(char *command)
{
	snd_pcm_format_t k;
	fprintf(stderr,
"Usage: %s [OPTION]... [FILE]...\n"
"\n"
"-h, --help              help\n"
"    --version           print current version\n"
"-l, --list-devices      list all soundcards and digital audio devices\n"
"-L, --list-pcms         list all PCMs defined\n"
"-D, --device=NAME       select PCM by name\n"
"-q, --quiet             quiet mode\n"
"-t, --file-type TYPE    file type (voc, wav, raw or au)\n"
"-c, --channels=#        channels\n"
"-f, --format=FORMAT     sample format (case insensitive)\n"
"-r, --rate=#            sample rate\n"
"-d, --duration=#        interrupt after # seconds\n"
"-s, --sleep-min=#       min ticks to sleep\n"
"-M, --mmap              mmap stream\n"
"-N, --nonblock          nonblocking mode\n"
"-F, --period-time=#     distance between interrupts is # microseconds\n"
"-B, --buffer-time=#     buffer duration is # microseconds\n"
"    --period-size=#     distance between interrupts is # frames\n"
"    --buffer-size=#     buffer duration is # frames\n"
"-A, --avail-min=#       min available space for wakeup is # microseconds\n"
"-R, --start-delay=#     delay for automatic PCM start is # microseconds \n"
"                        (relative to buffer size if <= 0)\n"
"-T, --stop-delay=#      delay for automatic PCM stop is # microseconds from xrun\n"
"-v, --verbose           show PCM structure and setup (accumulative)\n"
"-I, --separate-channels one file for each channel\n"
		, command);
	fprintf(stderr, "Recognized sample formats are:");
	/*for (k = 0; k < SND_PCM_FORMAT_LAST; ++k) {
		const char *s = snd_pcm_format_name(k);
		if (s)
			fprintf(stderr, " %s", s);
	}*/
	fprintf(stderr, "\nSome of these may not be available on selected hardware\n");
	fprintf(stderr, "The availabled format shortcuts are:\n");
	fprintf(stderr, "-f cd (16 bit little endian, 44100, stereo)\n");
	fprintf(stderr, "-f cdr (16 bit big endian, 44100, stereo)\n");
	fprintf(stderr, "-f dat (16 bit little endian, 48000, stereo)\n");
}

static void names_list(void)
{
	int err;
	snd_devname_t *list, *item;

	err = snd_names_list("pcm", &list);
	if (err < 0) {
		error("snd_names_list error: %s", snd_strerror(err));
		return;
	}
	item = list;
	while (item) {
		printf("%s [%s]\n", item->name, item->comment);
		item = item->next;
	}
	snd_names_list_free(list);
}

static void device_list(void)
{
	snd_ctl_t *handle;
	int card, err, dev, idx;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;
	snd_ctl_card_info_alloca(&info);
	snd_pcm_info_alloca(&pcminfo);

	card = -1;
	if (snd_card_next(&card) < 0 || card < 0) {
		error("no soundcards found...");
		return;
	}
	fprintf(stderr, "**** List of %s Hardware Devices ****\n", snd_pcm_stream_name(stream));
	while (card >= 0) {
		char name[32];
		sprintf(name, "hw:%d", card);
		if ((err = snd_ctl_open(&handle, name, 0)) < 0) {
			error("control open (%i): %s", card, snd_strerror(err));
			goto next_card;
		}
		if ((err = snd_ctl_card_info(handle, info)) < 0) {
			error("control hardware info (%i): %s", card, snd_strerror(err));
			snd_ctl_close(handle);
			goto next_card;
		}
		dev = -1;
		while (1) {
			unsigned int count;
			if (snd_ctl_pcm_next_device(handle, &dev)<0)
				error("snd_ctl_pcm_next_device");
			if (dev < 0)
				break;
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo, stream);
			if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
				if (err != -ENOENT)
					error("control digital audio info (%i): %s", card, snd_strerror(err));
				continue;
			}
			fprintf(stderr, "card %i: %s [%s], device %i: %s [%s]\n",
				card, snd_ctl_card_info_get_id(info), snd_ctl_card_info_get_name(info),
				dev,
				snd_pcm_info_get_id(pcminfo),
				snd_pcm_info_get_name(pcminfo));
			count = snd_pcm_info_get_subdevices_count(pcminfo);
			fprintf(stderr, "  Subdevices: %i/%i\n", snd_pcm_info_get_subdevices_avail(pcminfo), count);
			for (idx = 0; idx < (int)count; idx++) {
				snd_pcm_info_set_subdevice(pcminfo, idx);
				if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
					error("control digital audio playback info (%i): %s", card, snd_strerror(err));
				} else {
					fprintf(stderr, "  Subdevice #%i: %s\n", idx, snd_pcm_info_get_subdevice_name(pcminfo));
				}
			}
		}
		snd_ctl_close(handle);
	next_card:
		if (snd_card_next(&card) < 0) {
			error("snd_card_next");
			break;
		}
	}
}

static void pcm_list(void)
{
	snd_config_t *conf;
	snd_output_t *out;
	int err = snd_config_update();
	if (err < 0) {
		error("snd_config_update: %s", snd_strerror(err));
		return;
	}
	err = snd_output_stdio_attach(&out, stderr, 0);
	assert(err >= 0);
	err = snd_config_search(snd_config, "pcm", &conf);
	if (err < 0)
		return;
	fprintf(stderr, "PCM list:\n");
	snd_config_save(conf, out);
	snd_output_close(out);
}

static void signal_handler(int sig)
{
	if (!quiet_mode)
		fprintf(stderr, "Aborted by signal %s...\n", strsignal(sig));
	if (fd > 1) {
		close(fd);
		fd = -1;
	}
	if (handle) {
		snd_pcm_close(handle);
		handle = NULL;
	}
	exit(EXIT_FAILURE);
}

enum {
	OPT_VERSION = 1,
	OPT_PERIOD_SIZE,
	OPT_BUFFER_SIZE
};

static int playback_init(int argc, char *argv[])
{
	int option_index;
	char *short_options = "hnlLD:qt:c:f:r:d:s:MNF:A:R:T:B:vIPC";
	static struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, OPT_VERSION},
		{"list-devnames", 0, 0, 'n'},
		{"list-devices", 0, 0, 'l'},
		{"list-pcms", 0, 0, 'L'},
		{"device", 1, 0, 'D'},
		{"quiet", 0, 0, 'q'},
		{"file-type", 1, 0, 't'},
		{"channels", 1, 0, 'c'},
		{"format", 1, 0, 'f'},
		{"rate", 1, 0, 'r'},
		{"duration", 1, 0 ,'d'},
		{"sleep-min", 1, 0, 's'},
		{"mmap", 0, 0, 'M'},
		{"nonblock", 0, 0, 'N'},
		{"period-time", 1, 0, 'F'},
		{"period-size", 1, 0, OPT_PERIOD_SIZE},
		{"avail-min", 1, 0, 'A'},
		{"start-delay", 1, 0, 'R'},
		{"stop-delay", 1, 0, 'T'},
		{"buffer-time", 1, 0, 'B'},
		{"buffer-size", 1, 0, OPT_BUFFER_SIZE},
		{"verbose", 0, 0, 'v'},
		{"separate-channels", 0, 0, 'I'},
		{"playback", 0, 0, 'P'},
		{"capture", 0, 0, 'C'},
		{0, 0, 0, 0}
	};
	char *pcm_name = "default";
	int tmp, err, c;
	int do_names_list = 0, do_device_list = 0, do_pcm_list = 0;
	snd_pcm_info_t *info;

	snd_pcm_info_alloca(&info);

	command = argv[0];
	file_type = FORMAT_DEFAULT;


	chunk_size = -1;
	rhwparams.format = DEFAULT_FORMAT;
	rhwparams.rate = DEFAULT_SPEED;
	rhwparams.channels = 1;

	while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(command);
			return 0;
		case 'n':
			do_names_list = 1;
			break;
		case 'l':
			do_device_list = 1;
			break;
		case 'L':
			do_pcm_list = 1;
			break;
		case 'D':
			pcm_name = optarg;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		case 't':
			if (strcasecmp(optarg, "raw") == 0)
				file_type = FORMAT_RAW;
			else if (strcasecmp(optarg, "voc") == 0)
				file_type = FORMAT_VOC;
			else if (strcasecmp(optarg, "wav") == 0)
				file_type = FORMAT_WAVE;
			else if (strcasecmp(optarg, "au") == 0 || strcasecmp(optarg, "sparc") == 0)
				file_type = FORMAT_AU;
			else {
				error("unrecognized file format %s", optarg);
				return 1;
			}
			break;
		case 'c':
			rhwparams.channels = atoi(optarg);
			if (rhwparams.channels < 1 || rhwparams.channels > 32) {
				error("value %i for channels is invalid", rhwparams.channels);
				return 1;
			}
			break;
		case 'f':
			if (strcasecmp(optarg, "cd") == 0 || strcasecmp(optarg, "cdr") == 0) {
				if (strcasecmp(optarg, "cdr") == 0)
					rhwparams.format = SND_PCM_FORMAT_S16_BE;
				else
					rhwparams.format = file_type == FORMAT_AU ? SND_PCM_FORMAT_S16_BE : SND_PCM_FORMAT_S16_LE;
				rhwparams.rate = 44100;
				rhwparams.channels = 2;
			} else if (strcasecmp(optarg, "dat") == 0) {
				rhwparams.format = file_type == FORMAT_AU ? SND_PCM_FORMAT_S16_BE : SND_PCM_FORMAT_S16_LE;
				rhwparams.rate = 48000;
				rhwparams.channels = 2;
			} else {
				rhwparams.format = snd_pcm_format_value(optarg);
				if (rhwparams.format == SND_PCM_FORMAT_UNKNOWN) {
					error("wrong extended format '%s'", optarg);
					exit(EXIT_FAILURE);
				}
			}
			break;
		case 'r':
			tmp = atoi(optarg);
			if (tmp < 300)
				tmp *= 1000;
			rhwparams.rate = tmp;
			if (tmp < 2000 || tmp > 192000) {
				error("bad speed value %i", tmp);
				return 1;
			}
			break;
		case 'd':
			timelimit = atoi(optarg);
			break;
		case 's':
			sleep_min = atoi(optarg);
			break;
		case 'N':
			nonblock = 1;
			open_mode |= SND_PCM_NONBLOCK;
			break;
		case 'F':
			period_time = atoi(optarg);
			break;
		case 'B':
			buffer_time = atoi(optarg);
			break;
		case OPT_PERIOD_SIZE:
			period_frames = atoi(optarg);
			break;
		case OPT_BUFFER_SIZE:
			buffer_frames = atoi(optarg);
			break;
		case 'A':
			avail_min = atoi(optarg);
			break;
		case 'R':
			start_delay = atoi(optarg);
			break;
		case 'T':
			stop_delay = atoi(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'M':
			mmap_flag = 1;
			break;
		case 'I':
			interleaved = 0;
			break;
		case 'P':
			stream = SND_PCM_STREAM_PLAYBACK;
			command = "aplay";
			break;
		case 'C':
			stream = SND_PCM_STREAM_CAPTURE;
			command = "arecord";
			start_delay = 1;
			if (file_type == FORMAT_DEFAULT)
				file_type = FORMAT_WAVE;
			break;
		default:
			fprintf(stderr, "Try `%s --help' for more information.\n", command);
			return 1;
		}
	}

	if (do_names_list) {
		names_list();
		return 0;
	} else if (do_device_list) {
		if (do_pcm_list) pcm_list();
		device_list();
		snd_config_update_free_global();
		return 0;
	} else if (do_pcm_list) {
		pcm_list();
		snd_config_update_free_global();
		return 0;
	}

	err = snd_pcm_open(&handle, pcm_name, stream, open_mode);
	if (err < 0) {
		error("audio open error: %s", snd_strerror(err));
		return 1;
	}

	if ((err = snd_pcm_info(handle, info)) < 0) {
		error("info error: %s", snd_strerror(err));
		return 1;
	}

	if (nonblock) {
		err = snd_pcm_nonblock(handle, 1);
		if (err < 0) {
			error("nonblock setting error: %s", snd_strerror(err));
			return 1;
		}
	}

	chunk_size = 1024;
	hwparams = rhwparams;

	audiobuf = (char *)malloc(1024);
	if (audiobuf == NULL) {
		error("not enough memory");
		return 1;
	}

	if (mmap_flag) {
		writei_func = snd_pcm_mmap_writei;
		readi_func = snd_pcm_mmap_readi;
		writen_func = snd_pcm_mmap_writen;
		readn_func = snd_pcm_mmap_readn;
	} else {
		writei_func = snd_pcm_writei;
		readi_func = snd_pcm_readi;
		writen_func = snd_pcm_writen;
		readn_func = snd_pcm_readn;
	}


	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);
	playback_open();
}

static void set_params(void)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	unsigned int rate;
	snd_pcm_uframes_t start_threshold, stop_threshold;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		error("Broken configuration for this PCM: no configurations available");
		exit(EXIT_FAILURE);
	}
	if (mmap_flag) {
		snd_pcm_access_mask_t *mask = (snd_pcm_access_mask_t *)alloca(snd_pcm_access_mask_sizeof());
		snd_pcm_access_mask_none(mask);
		snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
		snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
		snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_COMPLEX);
		err = snd_pcm_hw_params_set_access_mask(handle, params, mask);
	} else if (interleaved)
		err = snd_pcm_hw_params_set_access(handle, params,
						   SND_PCM_ACCESS_RW_INTERLEAVED);
	else
		err = snd_pcm_hw_params_set_access(handle, params,
						   SND_PCM_ACCESS_RW_NONINTERLEAVED);
	if (err < 0) {
		error("Access type not available");
		exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_format(handle, params, hwparams.format);
	if (err < 0) {
		error("Sample format non available");
		exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_channels(handle, params, hwparams.channels);
	if (err < 0) {
		error("Channels count non available");
		exit(EXIT_FAILURE);
	}

#if 0
	err = snd_pcm_hw_params_set_periods_min(handle, params, 2);
	assert(err >= 0);
#endif
	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &hwparams.rate, 0);
	assert(err >= 0);
	if ((float)rate * 1.05 < hwparams.rate || (float)rate * 0.95 > hwparams.rate) {
		if (!quiet_mode) {
			fprintf(stderr, "Warning: rate is not accurate (requested = %iHz, got = %iHz)\n", rate, hwparams.rate);
			fprintf(stderr, "         please, try the plug plugin (-Dplug:%s)\n", snd_pcm_name(handle));
		}
	}
	rate = hwparams.rate;
	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params,
							    &buffer_time, 0);
		assert(err >= 0);
		if (buffer_time > 500000)
			buffer_time = 500000;
	}
	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}
	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, params,
							     &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, params,
							     &period_frames, 0);
	assert(err >= 0);
	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, params,
							     &buffer_time, 0);
	} else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, params,
							     &buffer_frames);
	}
	assert(err >= 0);
	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		error("Unable to install hw params:");
		exit(EXIT_FAILURE);
	}
	snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (chunk_size == buffer_size) {
		error("Can't use period equal to buffer size (%lu == %lu)", chunk_size, buffer_size);
		exit(EXIT_FAILURE);
	}
	snd_pcm_sw_params_current(handle, swparams);

	assert(err >= 0);
	if (avail_min < 0)
		n = chunk_size;
	else
		n = (double) rate * avail_min / 1000000;
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, n);

	/* round up to closest transfer boundary */
	n = buffer_size;
	if (start_delay <= 0) {
		start_threshold = n + (double) rate * start_delay / 1000000;
	} else
		start_threshold = (double) rate * start_delay / 1000000;
	if (start_threshold < 1)
		start_threshold = 1;
	if (start_threshold > n)
		start_threshold = n;
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	assert(err >= 0);
	if (stop_delay <= 0) 
		stop_threshold = buffer_size + (double) rate * stop_delay / 1000000;
	else
		stop_threshold = (double) rate * stop_delay / 1000000;
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	assert(err >= 0);

	assert(err >= 0);

	if (snd_pcm_sw_params(handle, swparams) < 0) {
		error("unable to install sw params:");
		exit(EXIT_FAILURE);
	}

	bits_per_sample = snd_pcm_format_physical_width(hwparams.format);
	bits_per_frame = bits_per_sample * hwparams.channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;
	audiobuf = (char *)realloc(audiobuf, chunk_bytes);
	if (audiobuf == NULL) {
		error("not enough memory");
		exit(EXIT_FAILURE);
	}
	// fprintf(stderr, "real chunk_size = %i, frags = %i, total = %i\n", chunk_size, setup.buf.block.frags, setup.buf.block.frags * chunk_size);
}

/* I/O error handler */
static void xrun(void)
{
	snd_pcm_status_t *status;
	int res;
	
	snd_pcm_status_alloca(&status);
	if ((res = snd_pcm_status(handle, status))<0) {
		error("status error: %s", snd_strerror(res));
		exit(EXIT_FAILURE);
	}
	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		struct timeval now, diff, tstamp;
		gettimeofday(&now, 0);
		snd_pcm_status_get_trigger_tstamp(status, &tstamp);
		timersub(&now, &tstamp, &diff);
		fprintf(stderr, "%s!!! (at least %.3f ms long)\n",
			stream == SND_PCM_STREAM_PLAYBACK ? "underrun" : "overrun",
			diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
		if (verbose) {
			fprintf(stderr, "Status:\n");
		}
		if ((res = snd_pcm_prepare(handle))<0) {
			error("xrun: prepare error: %s", snd_strerror(res));
			exit(EXIT_FAILURE);
		}
		return;		/* ok, data should be accepted again */
	} if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
		if (verbose) {
			fprintf(stderr, "Status(DRAINING):\n");
		}
		if (stream == SND_PCM_STREAM_CAPTURE) {
			fprintf(stderr, "capture stream format change? attempting recover...\n");
			if ((res = snd_pcm_prepare(handle))<0) {
				error("xrun(DRAINING): prepare error: %s", snd_strerror(res));
				exit(EXIT_FAILURE);
			}
			return;
		}
	}
	if (verbose) {
		fprintf(stderr, "Status(R/W):\n");
	}
	error("read/write error, state = %s", snd_pcm_state_name(snd_pcm_status_get_state(status)));
	exit(EXIT_FAILURE);
}

/* I/O suspend handler */
static void suspend(void)
{
	int res;

	if (!quiet_mode)
		fprintf(stderr, "Suspended. Trying resume. "); fflush(stderr);
	while ((res = snd_pcm_resume(handle)) == -EAGAIN)
		sleep(1);	/* wait until suspend flag is released */
	if (res < 0) {
		if (!quiet_mode)
			fprintf(stderr, "Failed. Restarting stream. "); fflush(stderr);
		if ((res = snd_pcm_prepare(handle)) < 0) {
			error("suspend: prepare error: %s", snd_strerror(res));
			exit(EXIT_FAILURE);
		}
	}
	if (!quiet_mode)
		fprintf(stderr, "Done.\n");
}

/* peak handler */
static void compute_max_peak(u_char *data, size_t count)
{
	signed int val, max, max_peak = 0, perc;
	size_t ocount = count;
	
	switch (bits_per_sample) {
	case 8: {
		signed char *valp = (signed char *)data;
		signed char mask = snd_pcm_format_silence(hwparams.format);
		while (count-- > 0) {
			val = *valp++ ^ mask;
			val = abs(val);
			if (max_peak < val)
				max_peak = val;
		}
		break;
	}
	case 16: {
		signed short *valp = (signed short *)data;
		signed short mask = snd_pcm_format_silence_16(hwparams.format);
		count /= 2;
		while (count-- > 0) {
			val = *valp++ ^ mask;
			val = abs(val);
			if (max_peak < val)
				max_peak = val;
		}
		break;
	}
	case 32: {
		signed int *valp = (signed int *)data;
		signed int mask = snd_pcm_format_silence_32(hwparams.format);
		count /= 4;
		while (count-- > 0) {
			val = *valp++ ^ mask;
			val = abs(val);
			if (max_peak < val)
				max_peak = val;
		}
		break;
	}
	default:
		break;
	}
	max = 1 << (bits_per_sample-1);
	if (max <= 0)
		max = 0x7fffffff;
	printf("Max peak (%li samples): %05i (0x%04x) ", (long)ocount, max_peak, max_peak);
	if (bits_per_sample > 16)
		perc = max_peak / (max / 100);
	else
		perc = max_peak * 100 / max;
	for (val = 0; val < 20; val++)
		if (val <= perc / 5)
			putc('#', stdout);
		else
			putc(' ', stdout);
	printf(" %i%%\n", perc);
}

/*
 *  write function
 */

static ssize_t pcm_write(u_char *data, size_t count)
{
	ssize_t r;
	ssize_t result = 0;

	if (sleep_min == 0 &&
	    count < chunk_size) {
		snd_pcm_format_set_silence(hwparams.format, data + count * bits_per_frame / 8, (chunk_size - count) * hwparams.channels);
		count = chunk_size;
	}
	while (count > 0) {
		r = writei_func(handle, data, count);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
			snd_pcm_wait(handle, 1000);
		} else if (r == -EPIPE) {
			xrun();
		} else if (r == -ESTRPIPE) {
			suspend();
		} else if (r < 0) {
			error("write error: %s", snd_strerror(r));
			exit(EXIT_FAILURE);
		}
		if (r > 0) {
			if (verbose > 1)
				compute_max_peak(data, r * hwparams.channels);
			result += r;
			count -= r;
			data += r * bits_per_frame / 8;
		}
	}
	return result;
}

/* setting the globals for playing raw data */
static void init_raw_data(void)
{
	hwparams = rhwparams;
}

/* calculate the data count to read from/to dsp */
static off_t calc_count(void)
{
	off_t count;

	if (timelimit == 0) {
		count = pbrec_count;
	} else {
		count = snd_pcm_format_size(hwparams.format, hwparams.rate * hwparams.channels);
		count *= (off_t)timelimit;
	}
	return count < pbrec_count ? count : pbrec_count;
}

static void end_raw(int fd)
{                              /* REALLY only close output */
       if (fd != 1)
               close(fd);
}

static void header(int rtype)
{
	if (!quiet_mode) {
		fprintf(stderr, "%s  : ", "Playing");
		fprintf(stderr, "%s, ", snd_pcm_format_description(hwparams.format));
		fprintf(stderr, "Rate %d Hz, ", hwparams.rate);
		if (hwparams.channels == 1)
			fprintf(stderr, "Mono");
		else if (hwparams.channels == 2)
			fprintf(stderr, "Stereo");
		else
			fprintf(stderr, "Channels %i", hwparams.channels);
		fprintf(stderr, "\n");
	}
}

static void playback_open() {
  init_raw_data();
  pbrec_count = calc_count();
  header(FORMAT_RAW);
  set_params();
}

static void playback_quit() {
  if (fd != 0)
    close(fd);
  snd_pcm_close(handle);
  free(audiobuf);
  snd_config_update_free_global();
}
