/*
 * Copyright(c) 2014 Tim Ruehsen
 * Copyright(c) 2015-2016 Free Software Foundation, Inc.
 *
 * This file is part of Wget.
 *
 * Wget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wget.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Progress bar routines
 *
 * Changelog
 * 11.09.2014  Tim Ruehsen  created
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

#include <wget.h>

#include "wget_options.h"
#include "wget_log.h"
#include "wget_bar.h"


// Rate at which progress thread it updated. This is the amount of time (in ms)
// for which the thread will sleep before waking up and redrawing the progress
enum { _BAR_THREAD_SLEEP_DURATION = 125 };

//Forward declaration for progress bar thread
static void *_bar_update_thread(void *p) G_GNUC_WGET_FLATTEN;

static void _error_write(const char *buf, size_t len);

static wget_bar_t
	*bar;
static wget_thread_mutex_t
	bar_mutex = WGET_THREAD_MUTEX_INITIALIZER;
static wget_thread_t
	progress_thread;

void bar_init(void)
{
	bar = wget_bar_init(NULL, config.max_threads + 1);

	// set custom write function for wget_error_printf()
	// _error_write uses 'bar', so that has to initialized before
	wget_logger_set_func(wget_get_logger(WGET_LOGGER_ERROR), _error_write);

	int rc = wget_thread_start(&progress_thread, _bar_update_thread, bar, 0);
	if (rc) {
		error_printf("Cannot create progress bar thread. Disabling progess bar");
		wget_bar_free(&bar);
		config.progress = 0;
	}

/*
	// set debug logging
	wget_logger_set_func(wget_get_logger(WGET_LOGGER_DEBUG), config.debug ? _write_debug : NULL);

	// set error logging
	wget_logger_set_stream(wget_get_logger(WGET_LOGGER_ERROR), config.quiet ? NULL : stderr);

	// set info logging
	wget_logger_set_stream(wget_get_logger(WGET_LOGGER_INFO), config.verbose && !config.quiet ? stdout : NULL);
*/
}

void bar_deinit(void)
{
	wget_thread_cancel(progress_thread);
	wget_thread_join(progress_thread);
	wget_bar_free(&bar);
}

void bar_print(int slotpos, const char *s)
{
	// This function will be called async from threads.
	// Cursor positioning might break without a mutex.
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_print(bar, slotpos, s);
	wget_thread_mutex_unlock(&bar_mutex);
}

void bar_vprintf(int slotpos, const char *fmt, va_list args)
{
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_vprintf(bar, slotpos, fmt, args);
	wget_thread_mutex_unlock(&bar_mutex);
}

void bar_printf(int slotpos, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	bar_vprintf(slotpos, fmt, args);
	va_end(args);
}

void bar_slot_register(wget_bar_ctx *bar_ctx, int slotpos)
{
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_slot_register(bar, bar_ctx, slotpos);
	wget_thread_mutex_unlock(&bar_mutex);
}

void bar_slot_begin(wget_bar_ctx *bar_ctx, const char *filename, ssize_t filesize)
{
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_slot_begin(bar, bar_ctx, filename, filesize);
	wget_thread_mutex_unlock(&bar_mutex);
}

void bar_slot_deregister(wget_bar_ctx *bar_ctx)
{
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_slot_deregister(bar, bar_ctx);
	wget_thread_mutex_unlock(&bar_mutex);
}

void bar_update_slots(void)
{
	wget_thread_mutex_lock(&bar_mutex);
	wget_bar_set_slots(bar, nthreads + 1);
	wget_thread_mutex_unlock(&bar_mutex);
}

static void *_bar_update_thread(void *p)
{
	wget_bar_t *prog_bar = (wget_bar_t *) p;

	for (;;) {
		wget_thread_mutex_lock(&bar_mutex);
		wget_bar_update(prog_bar);
		wget_thread_mutex_unlock(&bar_mutex);
		wget_millisleep(_BAR_THREAD_SLEEP_DURATION);
	}
	return NULL;
}

static void _error_write(const char *buf, size_t len)
{
//  printf("\033[s\033[1S\033[%dA\033[1G\033[2K", config.num_threads + 2);
	wget_thread_mutex_lock(&bar_mutex);
	printf("\033[s\033[1S\033[%dA\033[1G\033[0J", nthreads + 2);
	log_write_error_stdout(buf, len);
	printf("\033[u");
	fflush(stdout);
	wget_bar_update(bar);
	wget_thread_mutex_unlock(&bar_mutex);
}
