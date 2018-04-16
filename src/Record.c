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
#include <time.h>
#include <locale.h>
#include <alsa/asoundlib.h>
#include <assert.h>
#include <termios.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <endian.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#ifndef LLONG_MAX
#define LLONG_MAX    9223372036854775807LL
#endif

#define DEFAULT_FORMAT		SND_PCM_FORMAT_S16_LE  //SND_PCM_FORMAT_U8
#define DEFAULT_SPEED 		16000//8000

#define FORMAT_DEFAULT		-1
#define FORMAT_RAW		0
#define FORMAT_VOC		1
#define FORMAT_WAVE		2
#define FORMAT_AU		3

//added by AllenYuan
static int silflag = 0; //出现静音情况
static off64_t silcount = 0; //静音时要收到的包
static int enablesil = 0; //启用静音检测状态

static int quiet_mode = 0;
static int file_type = FORMAT_DEFAULT;
static int open_mode = 0;
static snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
static int mmap_flag = 0;
static int interleaved = 1;
static int nonblock = 0;
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
static int buffer_pos = 0;
static size_t significant_bits_per_sample, bits_per_sample, bits_per_frame;
static size_t chunk_bytes;
static int test_position = 0;
static int test_coef = 8;
static int test_nowait = 0;
static snd_output_t *log;
static long long max_file_size = 0;
static int max_file_time = 0;
static int use_strftime = 0;
volatile static int recycle_capture_file = 0;
static long term_c_lflag = -1;
static int dump_hw_params = 0;

static int fd = -1;
static off64_t pbrec_count = LLONG_MAX, fdcount;
static int vocmajor, vocminor;

static char *pidfile_name = NULL;
FILE *pidf = NULL;
static int pidfile_written = 0;

#ifdef CONFIG_SUPPORT_CHMAP
static snd_pcm_chmap_t *channel_map = NULL; /* chmap to override */
static unsigned int *hw_map = NULL; /* chmap to follow */
#endif

static off64_t pbrec_count = LLONG_MAX, fdcount;

enum {
	VUMETER_NONE, VUMETER_MONO, VUMETER_STEREO
};

static snd_pcm_t *handle;
static struct {
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} hwparams, rhwparams;

static const struct fmt_capture {
	void (*start)(int fd, size_t count);
	void (*end)(int fd);
	char *what;
	long long max_filesize;
} fmt_rec_table[] = { { NULL, NULL, N_("raw data"), LLONG_MAX }, {
		begin_voc,
		end_voc, N_("VOC"), 16000000LL
	},
	/* FIXME: can WAV handle exactly 2GB or less than it? */
	{ begin_wave, end_wave, N_("WAVE"), 2147483648LL }, { begin_au, end_au,
		N_("Sparc Audio"), LLONG_MAX
	}
};

/* global data */

static snd_pcm_sframes_t (*readi_func)(snd_pcm_t *handle, void *buffer,
                                       snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writei_func)(snd_pcm_t *handle, const void *buffer,
                                        snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*readn_func)(snd_pcm_t *handle, void **bufs,
                                       snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writen_func)(snd_pcm_t *handle, void **bufs,
                                        snd_pcm_uframes_t size);

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
	signal(sig, SIG_DFL);
}            

/* call on SIGUSR1 signal. */
static void signal_handler_recycle(int sig) {
	/* flag the capture loop to start a new output file */
	recycle_capture_file = 1;
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

static void capture(char *orig_name) {
	int tostdout = 0; /* boolean which describes output stream */
	int filecount = 0; /* number of files written */
	char *name = orig_name; /* current filename */
	char namebuf[PATH_MAX + 1];
	off64_t count, rest; /* number of bytes to capture */
	struct stat statbuf;

	/* get number of bytes to capture */
	count = calc_count();
	if (count == 0)
		count = LLONG_MAX;
	/* compute the number of bytes per file */
	max_file_size = max_file_time * snd_pcm_format_size(hwparams.format,
	                hwparams.rate * hwparams.channels);
	/* WAVE-file should be even (I'm not sure), but wasting one byte
	 isn't a problem (this can only be in 8 bit mono) */
	if (count < LLONG_MAX)
		count += count % 2;
	else
		count -= count % 2;

	/* display verbose output to console */
	//此处被Allen注释
	//header(file_type, name);

	/* setup sound hardware */
	set_params();

	/* write to stdout? */
	if (!name || !strcmp(name, "-")) {
		fd = fileno(stdout);
		name = "stdout";
		tostdout = 1;
		if (count > fmt_rec_table[file_type].max_filesize)
			count = fmt_rec_table[file_type].max_filesize;
	}
	init_stdin();

	//added by AllenYuan
	silcount = 2 * snd_pcm_format_size(hwparams.format, hwparams.rate
	                                   * hwparams.channels);

	do {
		/* open a file to write */
		if (!tostdout) {
			/* upon the second file we start the numbering scheme */
			if (filecount || use_strftime) {
				filecount = new_capture_file(orig_name, namebuf,
				                             sizeof(namebuf), filecount);
				name = namebuf;
			}

			/* open a new file */
			if (!lstat(name, &statbuf)) {
				if (S_ISREG(statbuf.st_mode))
					remove(name);
			}
			fd = safe_open(name);
			if (fd < 0) {
				perror(name);
				prg_exit(EXIT_FAILURE);
			}
			filecount++;
		}

		rest = count;
		if (rest > fmt_rec_table[file_type].max_filesize)
			rest = fmt_rec_table[file_type].max_filesize;
		if (max_file_size && (rest > max_file_size))
			rest = max_file_size;

		/* setup sample header */
		if (fmt_rec_table[file_type].start)
			fmt_rec_table[file_type].start(fd, rest);

		/* capture */
		fdcount = 0;
		while (rest > 0 && recycle_capture_file == 0 && !in_aborting) {
			size_t c = (rest <= (off64_t) chunk_bytes) ? (size_t) rest
			           : chunk_bytes;
			size_t f = c * 8 / bits_per_frame;
			if (pcm_read(audiobuf, f) != f)
				break;
			if (write(fd, audiobuf, c) != c) {
				perror(name);
				prg_exit(EXIT_FAILURE);
			}
			count -= c;
			rest -= c;
			fdcount += c;

			//added by AllenYuan
			if (silflag == 1 && enablesil == 1) //silence sil open
			{
				silcount -= c;
				if (silcount <= 0) //after 1 s
				{
					//game over
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

		/* finish sample container */
		if (fmt_rec_table[file_type].end && !tostdout) {
			fmt_rec_table[file_type].end(fd);
			fd = -1;
		}

		if (in_aborting)
			break;

		/* repeat the loop when format is raw without timelimit or
		 * requested counts of data are recorded
		 */
	} while ((file_type == FORMAT_RAW && !timelimit) || count > 0);
}


int main()
{
    char *pcm_name="plughw:0,0";
    snd_pcm_info_t *info;
    int err;
    FILE *direction;

    snd_pcm_info_alloca(&info);
    err = snd_output_stdio_attach(&log, stderr, 0);
    assert(err >= 0);

    file_type=FORMAT_DEFAULT;
    start_delay = 1;
	direction = stdout;
    chunk_size=-1;
    rhwparams.format = DEFAULT_FORMAT;
	//printf("%s\n",DEFAULT_FORMAT );
	rhwparams.rate = DEFAULT_SPEED;
	//printf("%s\n",DEFAULT_SPEED );
	rhwparams.channels = 1;
	rhwparams.rate = 16000;

    err = snd_pcm_open(&handle, pcm_name, stream, open_mode);
	if (err < 0) {
		error(_("audio open error: %s"), snd_strerror(err));
		return 1;
	}

	if ((err = snd_pcm_info(handle, info)) < 0) {
		error(_("info error: %s"), snd_strerror(err));
		return 1;
	}

    if (nonblock) {
		err = snd_pcm_nonblock(handle, 1);
		if (err < 0) {
			error(_("nonblock setting error: %s"), snd_strerror(err));
			return 1;
		}
	}

    chunk_size = 1024;
	hwparams = rhwparams;

    audiobuf = (u_char *) malloc(1024);
	if (audiobuf == NULL) {
		error(_("not enough memory"));
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

    pidfile_name="tests.pcm";

    if (pidfile_name) {
		errno = 0;
		pidf = fopen(pidfile_name, "w");
		if (pidf) {
			(void) fprintf(pidf, "%d\n", getpid());
			fclose(pidf);
			pidfile_written = 1;
		} else {
			error(_("Cannot create process ID file %s: %s"),
			      pidfile_name, strerror (errno));
			return 1;
		}
	}

    signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGUSR1, signal_handler_recycle);
    
}