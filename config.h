#include <netdb.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>       
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>

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
