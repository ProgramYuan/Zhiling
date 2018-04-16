#define _GNU_SOURCE
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <termios.h>
#include <signal.h>
#include <pthread.h>
#include <sys/shm.h>  
#include "shmdata.h"  
#include "aconfig.h"
#include "gettext.h"
#include "formats.h"
#include "version.h"

//#define Conditional 1

struct msg_audio
{
    u_char *AudioBuf;
};

u_char *tmpAudioBuf; //临时空间

#ifdef SND_CHMAP_API_VERSION
#define CONFIG_SUPPORT_CHMAP	1
#endif

#ifndef LLONG_MAX
#define LLONG_MAX    9223372036854775807LL
#endif

#ifndef le16toh
#include <asm/byteorder.h>
#define le16toh(x) __le16_to_cpu(x)
#define be16toh(x) __be16_to_cpu(x)
#define le32toh(x) __le32_to_cpu(x)
#define be32toh(x) __be32_to_cpu(x)
#endif

#define DEFAULT_FORMAT		SND_PCM_FORMAT_S16_LE  //SND_PCM_FORMAT_U8
#define DEFAULT_SPEED 		16000//8000

#define FORMAT_DEFAULT		-1
#define FORMAT_RAW		0
#define FORMAT_WAVE		2


/* global data */

static snd_pcm_sframes_t (*readi_func)(snd_pcm_t *handle, void *buffer,
                                       snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writei_func)(snd_pcm_t *handle, const void *buffer,
                                        snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*readn_func)(snd_pcm_t *handle, void **bufs,
                                       snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writen_func)(snd_pcm_t *handle, void **bufs,
                                        snd_pcm_uframes_t size);

enum {
	VUMETER_NONE, VUMETER_MONO, VUMETER_STEREO
};

static char *command;
static snd_pcm_t *handle;
static struct {
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} hwparams, rhwparams;
static int timelimit = 0;

static int silflag = 0; 
static off64_t silcount = 0; 
static int enablesil = 0; //启用静音检测状态

static int quiet_mode = 0;
static int file_type = FORMAT_DEFAULT;
static int open_mode = 0;
static snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
static int mmap_flag = 0;
static int interleaved = 1;
static volatile sig_atomic_t in_aborting = 0;
static u_char *audiobuf = NULL;
static snd_pcm_uframes_t chunk_size = 0;
static unsigned period_time = 0;   //中间的中断时间
static unsigned buffer_time = 0;
static snd_pcm_uframes_t period_frames = 0;
static snd_pcm_uframes_t buffer_frames = 0;
static int avail_min = -1;
static int start_delay = 0;
static int stop_delay = 0;
static int monotonic = 0;
static int interactive = 0;
static int can_pause = 0;
static int fatal_errors = 0;
static int verbose = 0;
static int vumeter = VUMETER_NONE;
static size_t significant_bits_per_sample, bits_per_sample, bits_per_frame;
static size_t chunk_bytes;
static int test_position = 0;
static int test_coef = 8;
static int test_nowait = 0;
static snd_output_t *log;
static int max_file_time = 0;
static int use_strftime = 0;
volatile static int recycle_capture_file = 0;
static long term_c_lflag = -1;
static int dump_hw_params = 0;

static int fd = -1;
static off64_t pbrec_count = LLONG_MAX;

#ifdef CONFIG_SUPPORT_CHMAP
static snd_pcm_chmap_t *channel_map = NULL; /* chmap to override */
static unsigned int *hw_map = NULL; /* chmap to follow */
#endif

/* needed prototypes */

static void done_stdin(void);

static void capture(char *filename);


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

/*
 *	Subroutine to clean up before exit.
 */
static void prg_exit(int code) {
	done_stdin();
	if (handle)
		snd_pcm_close(handle);
	exit(code);
}

static void signal_handler(int sig) {
	if (in_aborting)
		return;

	in_aborting = 1;
	if (verbose == 2)
		putchar('\n');
	if (!quiet_mode)
		fprintf(stderr, _("Aborted by signal %s...\n"), strsignal(sig));
	if (handle)
		snd_pcm_abort(handle);
	if (sig == SIGABRT) {
		/* do not call snd_pcm_close() and abort immediately */
		handle = NULL;
		prg_exit(EXIT_FAILURE);
	}
	// printf("handle:%d\n",handle);
	// printf("sig:%d\n",sig);
	// printf("sig1:%d\n",SIGABRT);
	// signal(sig, SIG_DFL);
}

/* call on SIGUSR1 signal. */
static void signal_handler_recycle(int sig) {
	/* flag the capture loop to start a new output file */
	recycle_capture_file = 1;
}

enum {
	OPT_VERSION = 1,
	OPT_PERIOD_SIZE,
	OPT_BUFFER_SIZE,
	OPT_DISABLE_RESAMPLE,
	OPT_DISABLE_CHANNELS,
	OPT_DISABLE_FORMAT,
	OPT_DISABLE_SOFTVOL,
	OPT_TEST_POSITION,
	OPT_TEST_COEF,
	OPT_TEST_NOWAIT,
	OPT_MAX_FILE_TIME,
	OPT_PROCESS_ID_FILE,
	OPT_USE_STRFTIME,
	OPT_DUMP_HWPARAMS,
	OPT_FATAL_ERRORS,
};

/*
 * Safe read (for pipes)
 */

static ssize_t safe_read(int fd, void *buf, size_t count) {
	ssize_t result = 0, res;

	while (count > 0 && !in_aborting) {
		if ((res = read(fd, buf, count)) == 0)
			break;
		if (res < 0)
			return result > 0 ? result : res;
		count -= res;
		result += res;
		buf = (char *) buf + res;
	}
	return result;
}

static void show_available_sample_formats(snd_pcm_hw_params_t* params) {
	snd_pcm_format_t format;

	fprintf(stderr, "Available formats:\n");
	for (format = 0; format <= SND_PCM_FORMAT_LAST; format++) {
		if (snd_pcm_hw_params_test_format(handle, params, format) == 0)
			fprintf(stderr, "- %s\n", snd_pcm_format_name(format));
	}
}

#ifdef CONFIG_SUPPORT_CHMAP
static int setup_chmap(void) {
	snd_pcm_chmap_t *chmap = channel_map;
	char mapped[hwparams.channels];
	snd_pcm_chmap_t *hw_chmap;
	unsigned int ch, i;
	int err;

	if (!chmap)
		return 0;

	if (chmap->channels != hwparams.channels) {
		error(_("Channel numbers don't match between hw_params and channel map"));
		return -1;
	}
	err = snd_pcm_set_chmap(handle, chmap);
	if (!err)
		return 0;

	hw_chmap = snd_pcm_get_chmap(handle);
	if (!hw_chmap) {
		fprintf(stderr, _("Warning: unable to get channel map\n"));
		return 0;
	}

	if (hw_chmap->channels == chmap->channels && !memcmp(hw_chmap, chmap, 4
	        * (chmap->channels + 1))) {
		/* maps are identical, so no need to convert */
		free(hw_chmap);
		return 0;
	}

	hw_map = calloc(hwparams.channels, sizeof(int));
	if (!hw_map) {
		error(_("not enough memory"));
		return -1;
	}

	memset(mapped, 0, sizeof(mapped));
	for (ch = 0; ch < hw_chmap->channels; ch++) {
		if (chmap->pos[ch] == hw_chmap->pos[ch]) {
			mapped[ch] = 1;
			hw_map[ch] = ch;
			continue;
		}
		for (i = 0; i < hw_chmap->channels; i++) {
			if (!mapped[i] && chmap->pos[ch] == hw_chmap->pos[i]) {
				mapped[i] = 1;
				hw_map[ch] = i;
				break;
			}
		}
		if (i >= hw_chmap->channels) {
			char buf[256];
			error(_("Channel %d doesn't match with hw_parmas"), ch);
			snd_pcm_chmap_print(hw_chmap, sizeof(buf), buf);
			fprintf(stderr, "hardware chmap = %s\n", buf);
			return -1;
		}
	}
	free(hw_chmap);
	return 0;
}
#else
#define setup_chmap()	0
#endif

static void set_params(void) {
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
		error(_("Broken configuration for this PCM: no configurations available"));
		prg_exit(EXIT_FAILURE);
	}
	if (dump_hw_params) {
		fprintf(stderr, _("HW Params of device \"%s\":\n"),
		        snd_pcm_name(handle));
		fprintf(stderr, "--------------------\n");
		snd_pcm_hw_params_dump(params, log);
		fprintf(stderr, "--------------------\n");
	}
	if (mmap_flag) {
		snd_pcm_access_mask_t *mask = alloca(snd_pcm_access_mask_sizeof());
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
		error(_("Access type not available"));
		prg_exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_format(handle, params, hwparams.format);
	if (err < 0) {
		error(_("Sample format non available"));
		show_available_sample_formats(params);
		prg_exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_channels(handle, params, hwparams.channels);
	if (err < 0) {
		error(_("Channels count non available"));
		prg_exit(EXIT_FAILURE);
	}

#if 0
	err = snd_pcm_hw_params_set_periods_min(handle, params, 2);
	assert(err >= 0);
#endif
	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &hwparams.rate, 0);
	assert(err >= 0);
	if ((float) rate * 1.05 < hwparams.rate || (float) rate * 0.95
	        > hwparams.rate) {
		if (!quiet_mode) {
			char plugex[64];
			const char *pcmname = snd_pcm_name(handle);
			fprintf(
			    stderr,
			    _("Warning: rate is not accurate (requested = %iHz, got = %iHz)\n"),
			    rate, hwparams.rate);
			if (!pcmname || strchr(snd_pcm_name(handle), ':'))
				*plugex = 0;
			else
				snprintf(plugex, sizeof(plugex), "(-Dplug:%s)", snd_pcm_name(
				             handle));
			fprintf(stderr, _("         please, try the plug plugin %s\n"),
			        plugex);
		}
	}
	rate = hwparams.rate;
	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0);
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
	monotonic = snd_pcm_hw_params_is_monotonic(params);
	can_pause = snd_pcm_hw_params_can_pause(params);
	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		error(_("Unable to install hw params:"));
		snd_pcm_hw_params_dump(params, log);
		prg_exit(EXIT_FAILURE);
	}
	snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (chunk_size == buffer_size) {
		error(_("Can't use period equal to buffer size (%lu == %lu)"),
		      chunk_size, buffer_size);
		prg_exit(EXIT_FAILURE);
	}
	snd_pcm_sw_params_current(handle, swparams);
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
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams,
	        start_threshold);
	assert(err >= 0);
	if (stop_delay <= 0)
		stop_threshold = buffer_size + (double) rate * stop_delay / 1000000;
	else
		stop_threshold = (double) rate * stop_delay / 1000000;
	err
	    = snd_pcm_sw_params_set_stop_threshold(handle, swparams,
	            stop_threshold);
	assert(err >= 0);

	if (snd_pcm_sw_params(handle, swparams) < 0) {
		error(_("unable to install sw params:"));
		snd_pcm_sw_params_dump(swparams, log);
		prg_exit(EXIT_FAILURE);
	}

	if (setup_chmap())
		prg_exit(EXIT_FAILURE);

	if (verbose)
		snd_pcm_dump(handle, log);

	bits_per_sample = snd_pcm_format_physical_width(hwparams.format);
	significant_bits_per_sample = snd_pcm_format_width(hwparams.format);
	bits_per_frame = bits_per_sample * hwparams.channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;
	audiobuf = realloc(audiobuf, chunk_bytes);
	if (audiobuf == NULL) {
		error(_("not enough memory"));
		prg_exit(EXIT_FAILURE);
	}
	// fprintf(stderr, "real chunk_size = %i, frags = %i, total = %i\n", chunk_size, setup.buf.block.frags, setup.buf.block.frags * chunk_size);

	/* stereo VU-meter isn't always available... */
	if (vumeter == VUMETER_STEREO) {
		if (hwparams.channels != 2 || !interleaved || verbose > 2)
			vumeter = VUMETER_MONO;
	}

	/* show mmap buffer arragment */
	if (mmap_flag && verbose) {
		const snd_pcm_channel_area_t *areas;
		snd_pcm_uframes_t offset, size = chunk_size;
		int i;
		err = snd_pcm_mmap_begin(handle, &areas, &offset, &size);
		if (err < 0) {
			error(_("snd_pcm_mmap_begin problem: %s"), snd_strerror(err));
			prg_exit(EXIT_FAILURE);
		}
		for (i = 0; i < hwparams.channels; i++)
			fprintf(stderr, "mmap_area[%i] = %p,%u,%u (%u)\n", i,
			        areas[i].addr, areas[i].first, areas[i].step,
			        snd_pcm_format_physical_width(hwparams.format));
		/* not required, but for sure */
		snd_pcm_mmap_commit(handle, offset, 0);
	}

	buffer_frames = buffer_size; /* for position test */
}

static void init_stdin(void) {
	struct termios term;
	long flags;

	if (!interactive)
		return;
	if (!isatty(fileno(stdin))) {
		interactive = 0;
		return;
	}
	tcgetattr(fileno(stdin), &term);
	term_c_lflag = term.c_lflag;
	if (fd == fileno(stdin))
		return;
	flags = fcntl(fileno(stdin), F_GETFL);
	if (flags < 0 || fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK) < 0)
		fprintf(stderr, _("stdin O_NONBLOCK flag setup failed\n"));
	term.c_lflag &= ~ICANON;
	tcsetattr(fileno(stdin), TCSANOW, &term);
}

static void done_stdin(void) {
	struct termios term;

	if (!interactive)
		return;
	if (fd == fileno(stdin) || term_c_lflag == -1)
		return;
	tcgetattr(fileno(stdin), &term);
	term.c_lflag = term_c_lflag;
	tcsetattr(fileno(stdin), TCSANOW, &term);
}

static void do_pause(void) {
	int err;
	unsigned char b;

	if (!can_pause) {
		fprintf(stderr, _("\rPAUSE command ignored (no hw support)\n"));
		return;
	}
	err = snd_pcm_pause(handle, 1);
	if (err < 0) {
		error(_("pause push error: %s"), snd_strerror(err));
		return;
	}
	while (1) {
		while (read(fileno(stdin), &b, 1) != 1)
			;
		if (b == ' ' || b == '\r') {
			while (read(fileno(stdin), &b, 1) == 1)
				;
			err = snd_pcm_pause(handle, 0);
			if (err < 0)
				error(_("pause release error: %s"), snd_strerror(err));
			return;
		}
	}
}

static void check_stdin(void) {
	unsigned char b;

	if (!interactive)
		return;
	if (fd != fileno(stdin)) {
		while (read(fileno(stdin), &b, 1) == 1) {
			if (b == ' ' || b == '\r') {
				while (read(fileno(stdin), &b, 1) == 1)
					;
				fprintf(
				    stderr,
				    _("\r=== PAUSE ===                                                            "));
				fflush(stderr);
				do_pause();
				fprintf(stderr,
				        "                                                                          \r");
				fflush(stderr);
			}
		}
	}
}

#ifndef timersub
#define	timersub(a, b, result) \
do { \
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
	if ((result)->tv_usec < 0) { \
		--(result)->tv_sec; \
		(result)->tv_usec += 1000000; \
	} \
} while (0)
#endif

#ifndef timermsub
#define	timermsub(a, b, result) \
do { \
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
	(result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
	if ((result)->tv_nsec < 0) { \
		--(result)->tv_sec; \
		(result)->tv_nsec += 1000000000L; \
	} \
} while (0)
#endif

/* I/O error handler */
static void xrun(void) {
	snd_pcm_status_t *status;
	int res;

	snd_pcm_status_alloca(&status);
	if ((res = snd_pcm_status(handle, status)) < 0) {
		error(_("status error: %s"), snd_strerror(res));
		prg_exit(EXIT_FAILURE);
	}
	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		if (fatal_errors) {
			error(_("fatal %s: %s"),
			      stream == SND_PCM_STREAM_PLAYBACK ? _("underrun") : _("overrun"),
			      snd_strerror(res));
			prg_exit(EXIT_FAILURE);
		}
		if (monotonic) {
#ifdef HAVE_CLOCK_GETTIME
			struct timespec now, diff, tstamp;
			clock_gettime(CLOCK_MONOTONIC, &now);
			snd_pcm_status_get_trigger_htstamp(status, &tstamp);
			timermsub(&now, &tstamp, &diff);
			fprintf(stderr, _("%s!!! (at least %.3f ms long)\n"),
			        stream == SND_PCM_STREAM_PLAYBACK ? _("underrun") : _("overrun"),
			        diff.tv_sec * 1000 + diff.tv_nsec / 1000000.0);
#else
			fprintf(stderr, "%s !!!\n", _("underrun"));
#endif
		} else {
			struct timeval now, diff, tstamp;
			gettimeofday(&now, 0);
			snd_pcm_status_get_trigger_tstamp(status, &tstamp);
			timersub(&now, &tstamp, &diff);
			fprintf(stderr, _("%s!!! (at least %.3f ms long)\n"), stream
			        == SND_PCM_STREAM_PLAYBACK ? _("underrun") : _("overrun"),
			        diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
		}
		if (verbose) {
			fprintf(stderr, _("Status:\n"));
			snd_pcm_status_dump(status, log);
		}
		if ((res = snd_pcm_prepare(handle)) < 0) {
			error(_("xrun: prepare error: %s"), snd_strerror(res));
			prg_exit(EXIT_FAILURE);
		}
		return; /* ok, data should be accepted again */
	}
	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
		if (verbose) {
			fprintf(stderr, _("Status(DRAINING):\n"));
			snd_pcm_status_dump(status, log);
		}
		if (stream == SND_PCM_STREAM_CAPTURE) {
			fprintf(
			    stderr,
			    _("capture stream format change? attempting recover...\n"));
			if ((res = snd_pcm_prepare(handle)) < 0) {
				error(_("xrun(DRAINING): prepare error: %s"), snd_strerror(res));
				prg_exit(EXIT_FAILURE);
			}
			return;
		}
	}
	if (verbose) {
		fprintf(stderr, _("Status(R/W):\n"));
		snd_pcm_status_dump(status, log);
	}
	error(_("read/write error, state = %s"), snd_pcm_state_name(snd_pcm_status_get_state(status)));
	prg_exit(EXIT_FAILURE);
}

/* I/O suspend handler */
static void suspend(void) {
	int res;

	if (!quiet_mode)
		fprintf(stderr, _("Suspended. Trying resume. "));
	fflush(stderr);
	while ((res = snd_pcm_resume(handle)) == -EAGAIN)
		sleep(1); /* wait until suspend flag is released */
	if (res < 0) {
		if (!quiet_mode)
			fprintf(stderr, _("Failed. Restarting stream. "));
		fflush(stderr);
		if ((res = snd_pcm_prepare(handle)) < 0) {
			error(_("suspend: prepare error: %s"), snd_strerror(res));
			prg_exit(EXIT_FAILURE);
		}
	}
	if (!quiet_mode)
		fprintf(stderr, _("Done.\n"));
}

static void print_vu_meter_mono(int perc, int maxperc) {
	const int bar_length = 50;
	char line[80];
	int val;

	if (perc >= 15) 
	{
		enablesil = 1; //sel open
		silflag = 0;
	} else if (silflag == 0) {
		silcount = (1 * snd_pcm_format_size(hwparams.format, hwparams.rate * hwparams.channels))/3;
		silflag = 1;
	} else {

	}

	#ifdef Conditional
		for (val = 0; val <= perc * bar_length / 100 && val < bar_length; val++)
			line[val] = '#';
		for (; val <= maxperc * bar_length / 100 && val < bar_length; val++)
			line[val] = ' ';
		line[val] = '+';
		for (++val; val <= bar_length; val++)
			line[val] = ' ';
		if (maxperc > 99)
			sprintf(line + val, "| MAX");
		else
			sprintf(line + val, "| %02i%%", maxperc);
		fputs(line, stderr);
		if (perc > 100)
			fprintf(stderr, _(" !clip  "));
	#endif
}

static void print_vu_meter_stereo(int *perc, int *maxperc) {
	const int bar_length = 35;
	char line[80];
	int c;

	memset(line, ' ', sizeof(line) - 1);
	line[bar_length + 3] = '|';

	for (c = 0; c < 2; c++) {
		int p = perc[c] * bar_length / 100;
		char tmp[4];
		if (p > bar_length)
			p = bar_length;
		if (c)
			memset(line + bar_length + 6 + 1, '#', p);
		else
			memset(line + bar_length - p - 1, '#', p);
		p = maxperc[c] * bar_length / 100;
		if (p > bar_length)
			p = bar_length;
		if (c)
			line[bar_length + 6 + 1 + p] = '+';
		else
			line[bar_length - p - 1] = '+';
		if (maxperc[c] > 99)
			sprintf(tmp, "MAX");
		else
			sprintf(tmp, "%02d%%", maxperc[c]);
		if (c)
			memcpy(line + bar_length + 3 + 1, tmp, 3);
		else
			memcpy(line + bar_length, tmp, 3);
	}
	line[bar_length * 2 + 6 + 2] = 0;
	fputs(line, stderr);
}

static void print_vu_meter(signed int *perc, signed int *maxperc) {
	if (vumeter == VUMETER_STEREO)
		print_vu_meter_stereo(perc, maxperc);
	else
		print_vu_meter_mono(*perc, *maxperc);
}

/* peak handler */
static void compute_max_peak(u_char *data, size_t count) {
	signed int val, max, perc[2], max_peak[2];
	static int run = 0;
	size_t ocount = count;
	int format_little_endian = snd_pcm_format_little_endian(hwparams.format);
	int ichans, c;

	if (vumeter == VUMETER_STEREO)
		ichans = 2;
	else
		ichans = 1;

	memset(max_peak, 0, sizeof(max_peak));
	switch (bits_per_sample) {
	case 8: {
		signed char *valp = (signed char *) data;
		signed char mask = snd_pcm_format_silence(hwparams.format);
		c = 0;
		while (count-- > 0) {
			val = *valp++ ^ mask;
			val = abs(val);
			if (max_peak[c] < val)
				max_peak[c] = val;
			if (vumeter == VUMETER_STEREO)
				c = !c;
		}
		break;
	}
	case 16: {
		signed short *valp = (signed short *) data;
		signed short mask = snd_pcm_format_silence_16(hwparams.format);
		signed short sval;

		count /= 2;
		c = 0;
		while (count-- > 0) {
			if (format_little_endian)
				sval = le16toh(*valp);
			else
				sval = be16toh(*valp);
			sval = abs(sval) ^ mask;
			if (max_peak[c] < sval)
				max_peak[c] = sval;
			valp++;
			if (vumeter == VUMETER_STEREO)
				c = !c;
		}
		break;
	}
	case 24: {
		unsigned char *valp = data;
		signed int mask = snd_pcm_format_silence_32(hwparams.format);

		count /= 3;
		c = 0;
		while (count-- > 0) {
			if (format_little_endian) {
				val = valp[0] | (valp[1] << 8) | (valp[2] << 16);
			} else {
				val = (valp[0] << 16) | (valp[1] << 8) | valp[2];
			}
			/* Correct signed bit in 32-bit value */
			if (val & (1 << (bits_per_sample - 1))) {
				val |= 0xff << 24; /* Negate upper bits too */
			}
			val = abs(val) ^ mask;
			if (max_peak[c] < val)
				max_peak[c] = val;
			valp += 3;
			if (vumeter == VUMETER_STEREO)
				c = !c;
		}
		break;
	}
	case 32: {
		signed int *valp = (signed int *) data;
		signed int mask = snd_pcm_format_silence_32(hwparams.format);

		count /= 4;
		c = 0;
		while (count-- > 0) {
			if (format_little_endian)
				val = le32toh(*valp);
			else
				val = be32toh(*valp);
			val = abs(val) ^ mask;
			if (max_peak[c] < val)
				max_peak[c] = val;
			valp++;
			if (vumeter == VUMETER_STEREO)
				c = !c;
		}
		break;
	}
	default:
		if (run == 0) {
			fprintf(stderr, _("Unsupported bit size %d.\n"),
			        (int) bits_per_sample);
			run = 1;
		}
		return;
	}
	max = 1 << (significant_bits_per_sample - 1);
	if (max <= 0)
		max = 0x7fffffff;

	for (c = 0; c < ichans; c++) {
		if (bits_per_sample > 16)
			perc[c] = max_peak[c] / (max / 100);
		else
			perc[c] = max_peak[c] * 100 / max;
	}

	if (interleaved && verbose <= 2) {
		static int maxperc[2];
		static time_t t = 0;
		const time_t tt = time(NULL);
		if (tt > t) {
			t = tt;
			maxperc[0] = 0;
			maxperc[1] = 0;
		}
		for (c = 0; c < ichans; c++)
			if (perc[c] > maxperc[c])
				maxperc[c] = perc[c];

		putc('\r', stderr);
		print_vu_meter(perc, maxperc);
		fflush(stderr);
	} else if (verbose == 3) {
		fprintf(stderr, _("Max peak (%li samples): 0x%08x "), (long) ocount,
		        max_peak[0]);
		for (val = 0; val < 20; val++)
			if (val <= perc[0] / 5)
				putc('#', stderr);
			else
				putc(' ', stderr);
		fprintf(stderr, " %i%%\n", perc[0]);
		fflush(stderr);
	}
}

/*
 */
#ifdef CONFIG_SUPPORT_CHMAP
static u_char *remap_data(u_char *data, size_t count) {
	static u_char *tmp, *src, *dst;
	static size_t tmp_size;
	size_t sample_bytes = bits_per_sample / 8;
	size_t step = bits_per_frame / 8;
	size_t chunk_bytes;
	unsigned int ch, i;

	if (!hw_map)
		return data;

	chunk_bytes = count * bits_per_frame / 8;
	if (tmp_size < chunk_bytes) {
		free(tmp);
		tmp = malloc(chunk_bytes);
		if (!tmp) {
			error(_("not enough memory"));
			exit(1);
		}
		tmp_size = count;
	}

	src = data;
	dst = tmp;
	for (i = 0; i < count; i++) {
		for (ch = 0; ch < hwparams.channels; ch++) {
			memcpy(dst, src + sample_bytes * hw_map[ch], sample_bytes);
			dst += sample_bytes;
		}
		src += step;
	}
	return tmp;
}

static u_char **remap_datav(u_char **data, size_t count) {
	static u_char **tmp;
	unsigned int ch;

	if (!hw_map)
		return data;

	if (!tmp) {
		tmp = malloc(sizeof(*tmp) * hwparams.channels);
		if (!tmp) {
			error(_("not enough memory"));
			exit(1);
		}
		for (ch = 0; ch < hwparams.channels; ch++)
			tmp[ch] = data[hw_map[ch]];
	}
	return tmp;
}
#else
#define remap_data(data, count)		(data)
#define remap_datav(data, count)	(data)
#endif

/*
 *  read function
 */

static ssize_t pcm_read(u_char *data, size_t rcount) {
	ssize_t r;
	size_t result = 0;
	size_t count = rcount;

	if (count != chunk_size) {
		count = chunk_size;
	}

	while (count > 0 && !in_aborting) {
		check_stdin();
		r = readi_func(handle, data, count);
		if (r == -EAGAIN || (r >= 0 && (size_t) r < count)) {
			if (!test_nowait)
				snd_pcm_wait(handle, 100);
		} else if (r == -EPIPE) {
			xrun();
		} else if (r == -ESTRPIPE) {
			suspend();
		} else if (r < 0) {
			error(_("read error: %s"), snd_strerror(r));
			prg_exit(EXIT_FAILURE);
		}
		if (r > 0) {
			if (vumeter)
				compute_max_peak(data, r * hwparams.channels);
			result += r;
			count -= r;
			data += r * bits_per_frame / 8;
		}
	}
	return rcount;
}

/* setting the globals for playing raw data */
static void init_raw_data(void) {
	hwparams = rhwparams;
}

/* calculate the data count to read from/to dsp */
static off64_t calc_count(void) {
	off64_t count;

	if (timelimit == 0) {
		count = pbrec_count;
	} else {
		count = snd_pcm_format_size(hwparams.format, hwparams.rate
		                            * hwparams.channels);
		count *= (off64_t) timelimit;
	}
	return count < pbrec_count ? count : pbrec_count;
}


int create_path(const char *path) {
	char *start;
	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	if (path[0] == '/')
		start = strchr(path + 1, '/');
	else
		start = strchr(path, '/');

	while (start) {
		char *buffer = strdup(path);
		buffer[start - path] = 0x00;

		if (mkdir(buffer, mode) == -1 && errno != EEXIST) {
			fprintf(stderr, "Problem creating directory %s", buffer);
			perror(" ");
			free(buffer);
			return -1;
		}
		free(buffer);
		start = strchr(start + 1, '/');
	}
	return 0;
}

static int safe_open(const char *name) {
	int fd;

	fd = open(name, O_WRONLY | O_CREAT, 0644);
	if (fd == -1) {
		if (errno != ENOENT || !use_strftime)
			return -1;
		if (create_path(name) == 0)
			fd = open(name, O_WRONLY | O_CREAT, 0644);
	}
	return fd;
}

/**
 ** 字符串拼接方法
 ** Coded by Allen Yuan
 **/
char * str_contact(const char *str1, const char *str2) {
	char * result;
	result = (char*) malloc(strlen(str1) + strlen(str2) + 1); //str1的长度 + str2的长度 + \0;
	if (!result) { //如果内存动态分配失败
		printf("Error: malloc failed in concat! \n");
		exit(EXIT_FAILURE);
	}
	strcpy(result, str1);
	strcat(result, str2); //字符串拼接
	return result;
}

/**
 * 8位随机字符串
 */
char* generate(int len) {
	//	uuid_t uuid;
	//	char uuid_str[37];
	//	uuid_generate(uuid);
	//	uuid_unparse(uuid, uuid_str);
	//	printf("%f\n", duration2);
	/*产生密码用的字符串*/
	srand(time(0));
	char *buffer = (char*) malloc(9); /*分配内存*/
	memset(buffer,0,9);
	static const char string[] =
			"0123456789abcdefghiljklnopqrstuvwxyz";
	int i = 0;
	for (; i < len; i++) {
		buffer[i] = string[rand() % strlen(string)]; /*产生随机数*/
	}
	return buffer;
}


static void capture(char *orig_name) {
	char *name = orig_name; /* 当前文件名 */
	off64_t count, rest; /* 要捕获的字节数 */

	/* 获取要捕获的字节数 */
	count = calc_count();
	if (count == 0)
		count = LLONG_MAX;
	/* WAVE-file should be even (I'm not sure), but wasting one byte
	 isn't a problem (this can only be in 8 bit mono) */
	if (count < LLONG_MAX)
		count += count % 2;
	else
		count -= count % 2;

	/* 设置声音硬件 */
	set_params();

	init_stdin();
    
	silcount = (1 * snd_pcm_format_size(hwparams.format, hwparams.rate * hwparams.channels))/3;

    //测试
    int ssr=1;
    struct msg_audio audio_data[41];
    int a_size=0;
	do {
		rest = count;
		/* capture */
		while (rest > 0 && recycle_capture_file == 0 && !in_aborting) {
			size_t c = (rest <= (off64_t) chunk_bytes) ? (size_t) rest
			           : chunk_bytes;
			size_t f = c * 8 / bits_per_frame;
			if (pcm_read(audiobuf, f) != f)
				break;
            //输出Buf
            a_size=c;
            audio_data[ssr].AudioBuf=(u_char *)malloc(a_size);
            memcpy(audio_data[ssr].AudioBuf,audiobuf,a_size);

            ssr++;
			printf("%d",ssr);
			count -= c;
			rest -= c;

			if (silflag == 1 && enablesil == 1) //silence sil open
			{
				silcount -= c;
				if (silcount <= 0) //after 1 s
				{
					count = 0;
					rest = 0;
				}
			}

		}
		/* re-enable SIGUSR1 signal */
		if (recycle_capture_file) {
			recycle_capture_file = 0;
			signal(SIGUSR1, signal_handler_recycle);
		}

		if (in_aborting)
			break;

	printf("\n");
	} while ((file_type == FORMAT_RAW && !timelimit) || count > 0);

    int sk=0;
	tmpAudioBuf=(u_char *)malloc(a_size);
	if(ssr>6)
	{
		char *wavFileName = (char*) malloc(20);
		wavFileName =str_contact("./wav/" ,strcat(generate(6), ".pcm"));
		fd = safe_open(wavFileName);
		if (fd < 0) {
			perror(wavFileName);
			prg_exit(EXIT_FAILURE);
		}

		void *shm = NULL;  
		struct shared_use_st *shared = NULL;  
		int shmid;   
		shmid = shmget((key_t)33225, sizeof(struct shared_use_st), 0666|IPC_CREAT);  
		if(shmid == -1)  
		{  
			fprintf(stderr, "shmget failed\n");  
			exit(EXIT_FAILURE);  
		}  
		shm = shmat(shmid, (void*)0, 0);  
		if(shm == (void*)-1)  
		{  
			fprintf(stderr, "shmat failed\n");  
			exit(EXIT_FAILURE);  
		}  
		//printf("Memory attached at %X\n", (int)shm); 
		shared = (struct shared_use_st*)shm; 
		if(shared->canwrite == 1)
		{
			audio_data[0].AudioBuf=(u_char *)malloc(a_size);
            memcpy(audio_data[0].AudioBuf,tmpAudioBuf,a_size);

			for(sk=0;sk<ssr;sk++)
			{
				while(shared->written == 1)  
				{  
					;
				}   
				memcpy(shared->AudioBuf,audio_data[sk].AudioBuf,a_size);

				if (write(fd, shared->AudioBuf, a_size) != a_size) {
					perror(name);
					prg_exit(EXIT_FAILURE);
				}

				shared->flag=sk==0?1:(sk==(ssr-1)?3:2);
				shared->written = 1;  
				
			}
		} 
		if(shmdt(shm) == -1)  
		{  
			fprintf(stderr, "shmdt failed\n");  
			exit(EXIT_FAILURE);  
		}  
		//exit(EXIT_SUCCESS);
	}
	else
	{
		//保存到临时空间
		memcpy(tmpAudioBuf,audiobuf,a_size);
	}
    
}


int main(int argc, char *argv[]) {
	char *pcm_name = "plughw:0,0";
	int tmp, err, c;

	command = "arecord";
	timelimit = 5;  //5秒
	start_delay = 1;
	chunk_size = -1;
	stream = SND_PCM_STREAM_CAPTURE;
	file_type = FORMAT_WAVE;
	
	rhwparams.format = DEFAULT_FORMAT;
	rhwparams.rate = DEFAULT_SPEED;
	rhwparams.channels = 1;
	rhwparams.rate = 16000;

	vumeter = VUMETER_MONO;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGUSR1, signal_handler_recycle);
	signal(SIGINT,SIG_DFL);
	
		snd_pcm_info_t *info;
		snd_pcm_info_alloca(&info);

		err = snd_output_stdio_attach(&log, stderr, 0);
		assert(err >= 0);

		err = snd_pcm_open(&handle, pcm_name, stream, open_mode);
		if (err < 0) {
			error(_("audio open error: %s"), snd_strerror(err));
			return 1;
		}

		if ((err = snd_pcm_info(handle, info)) < 0) {
			error(_("info error: %s"), snd_strerror(err));
			return 1;
		}

		chunk_size = 1024;
		hwparams = rhwparams;

		audiobuf = (u_char *) malloc(1024);
		if (audiobuf == NULL) {
			error(_("not enough memory"));
			return 1;
		}

		writei_func = snd_pcm_writei;
		readi_func = snd_pcm_readi;
		writen_func = snd_pcm_writen;
		readn_func = snd_pcm_readn;
		// signal(SIGINT, signal_handler);
		// signal(SIGTERM, signal_handler);
		// signal(SIGABRT, signal_handler);
		// signal(SIGUSR1, signal_handler_recycle);
		//capture("stdout");
        off64_t count,rest;

        set_params();
        init_stdin();

        silcount = (1 * snd_pcm_format_size(hwparams.format, hwparams.rate * hwparams.channels))/3;
        while(1) 
        {
            printf("hello");
            int ssr=1;
            int a_size=0;
            count = snd_pcm_format_size(hwparams.format, hwparams.rate
		                            * hwparams.channels);
            if(count==0)
                count=LLONG_MAX;
            if(count < LLONG_MAX)
                count += count % 2;
            else
                count -= count % 2;
            rest = count;
            /* capture */
            while (rest > 0 && recycle_capture_file == 0 && !in_aborting) {
                size_t c = (rest <= (off64_t) chunk_bytes) ? (size_t) rest
                        : chunk_bytes;
                size_t f = c * 8 / bits_per_frame;
                if (pcm_read(audiobuf, f) != f)
                    break;
                //输出Buf
                a_size=c;
                //audio_data[ssr].AudioBuf=(u_char *)malloc(a_size);
                //memcpy(audio_data[ssr].AudioBuf,audiobuf,a_size);

                ssr++;
                printf("%d",ssr);
                count -= c;
                rest -= c;

                if (silflag == 1 && enablesil == 1) //silence sil open
                {
                    silcount -= c;
                    if (silcount <= 0) //after 1 s
                    {
                        count = 0;
                        rest = 0;
                    }
                }

            }
            /* re-enable SIGUSR1 signal */
            if (recycle_capture_file) {
                recycle_capture_file = 0;
                signal(SIGUSR1, signal_handler_recycle);
            }

            if (in_aborting)
                break;

            printf("\n");
        }



		if (verbose == 2)
			putchar('\n');
		snd_pcm_close(handle);
		handle = NULL;
		free(audiobuf);
	
}
