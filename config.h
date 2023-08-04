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

#ifndef DETACHTTY_CONFIG_H
#define DETACHTTY_CONFIG_H

#include <netdb.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifdef NEED_PTY_H
#include <pty.h>
#endif

#ifdef NEED_LIBUTIL_H
#include <libutil.h>
#endif

#ifdef NEED_STRINGS_H
#include <strings.h>
#endif

#ifdef NEED_LOCAL_FORKPTY_H
#include "forkpty.h"
#endif

#define DETACHTTY_VERSION_STR "11.0.0"

int copy_a_bit(int in_fd, int out_fd, int dribble_fd, char *message);
int copy_a_bit_with_log(int in_fd, int out_fd, int dribble_fd,
                        char * program, char *message);

int copy_a_bit_sendfd(int in_fd, int out_fd,
                      int dribble_fd, int send_fd, char *message);
int copy_a_bit_recvfd_with_log(int in_fd, int out_fd,
                               int dribble_fd, int * recv_fd,
                               char * program, char *message);

int output_buffer(int out_fd);
int output_buffer_sendfd(int out_fd, int send_fd);

extern FILE *log_fp;
int logprintf(char *progname, char *format, ...);
void bail(char *progname, char *format, ...);

#endif /* DETACHTTY_CONFIG_H */
