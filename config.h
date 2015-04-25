#include <netdb.h>
#include <sys/poll.h>
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

int copy_a_bit(int in_fd, int out_fd, int dribble_fd, char *message);
int copy_a_bit_with_log(int in_fd, int out_fd, int dribble_fd, char * program, char *message);
int output_buffer(int fd);
int logprintf(char *progname, char *fmt,...);
void bail(char *progname,char *fmt,...);
