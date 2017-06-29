/*
 * detachtty - run long-lived processes non interactively in the background,
 *             and reattach them to a terminal at later time
 *
 * Copyright (C) 2016-2017 Massimiliano Ghilardi
 * Copyright (C) 2001-2005 Daniel Barlow
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License
 *     as published by the Free Software Foundation; either version 2
 *     of the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

FILE *log_fp = NULL;

int logprintf(char *progname, char *format,...) {
    va_list ap;
    int n;
    va_start(ap, format);
    fprintf(log_fp, ";;; %s: %ld: ", progname, time(NULL));
    n=vfprintf(log_fp, format, ap);
    va_end(ap);
    fputs("\r\n",log_fp);
    return n;
}

void bail(char *progname,char *format,...) {
    va_list ap;
    int err = errno;
    va_start(ap, format);
    fprintf(log_fp,";;; %s: %ld: FATAL ", progname, time(NULL));
    vfprintf(log_fp, format, ap);
    va_end(ap);
    /* use \r\n to avoid staircase effect */
    if (err > 0) 
	fprintf(log_fp, " (%s)\r\n", strerror(err));
    else
	fputs("\r\n",log_fp);

    kill(getpid(), SIGTERM);
}

