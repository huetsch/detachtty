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

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "config.h"

#if defined(SCM_RIGHTS) && defined(CMSG_FIRSTHDR) && defined(CMSG_LEN) && defined(CMSG_DATA) && defined(CMSG_NXTHDR)
# define DETACHTTY_SENDFD_RECVFD
#else
# warning compiling without SENDFD/RECVD support
#endif


enum { buf_capacity = 4096 };
static int bytes_in_buf = 0;
static char buf[buf_capacity + 1];

#ifdef DETACHTTY_SENDFD_RECVFD
int send_bytes_and_fd(int out_fd, const char * bytes, int bytes_to_write, int send_fd)
{
    char control[sizeof(struct cmsghdr)+sizeof(int)];
    struct msghdr  msg;
    struct cmsghdr *cmsg;
    struct iovec   iov;

    if (send_fd < 0)
        return -1;

    /* Response data */
    iov.iov_base = (char *)bytes;
    iov.iov_len  = bytes_to_write;

    /* compose the message */
    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    /* attach open in_fd */
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &send_fd, sizeof(int));

    msg.msg_controllen = cmsg->cmsg_len;

    return sendmsg(out_fd, &msg, 0);
}

int recv_bytes_and_fd(int in_fd, char * bytes, int bytes_to_read, int * recv_fd) {
    char control[sizeof(struct cmsghdr)+sizeof(int)+100];
    struct msghdr  msg;
    struct cmsghdr *cmsg;
    struct iovec   iov;

    if (recv_fd == NULL)
        return -1;

    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));
    iov.iov_base   = bytes;
    iov.iov_len    = bytes_to_read;
    msg.msg_iov    = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    bytes_to_read = recvmsg(in_fd, &msg, 0);
    if (bytes_to_read < 0)
        return bytes_to_read;

    /* Loop over all control messages */
    cmsg = CMSG_FIRSTHDR(&msg);
    while (cmsg != NULL) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type  == SCM_RIGHTS) {
          memcpy(recv_fd, CMSG_DATA(cmsg), sizeof(int));
          break;
      }
      cmsg = CMSG_NXTHDR(&msg, cmsg);
    }
    return bytes_to_read < (int)iov.iov_len ? bytes_to_read : (int)iov.iov_len;
}
#endif /* DETACHTTY_SENDFD_RECVFD */


int full_write_bytes_and_sendfd(int out_fd, const char * bytes, int bytes_to_write, int send_fd) {
    int bytes_written = 0, bytes_written_total = 0;
    if (out_fd < 0 || bytes_to_write <= 0)
        return 0;

#ifdef DETACHTTY_SENDFD_RECVFD
    if (send_fd >= 0 && send_bytes_and_fd(out_fd, bytes, 1, send_fd) >= 0) {
        bytes++;
        bytes_written_total++;
        bytes_to_write--;
    }
#endif

    while (bytes_to_write > 0) {
	bytes_written = write(out_fd, bytes, bytes_to_write);
	if (bytes_written < 0)
            return bytes_written;
	bytes_to_write -= bytes_written;
        bytes += bytes_written;
        bytes_written_total += bytes_written;
    }
    return bytes_written_total;
}

int output_buffer(int out_fd) {
    return full_write_bytes_and_sendfd(out_fd, buf, bytes_in_buf, -1);
}

int output_buffer_sendfd(int out_fd, int send_fd) {
    return full_write_bytes_and_sendfd(out_fd, buf, bytes_in_buf, send_fd);
}





int input_buffer_recvfd(int in_fd, int dribble_fd, int * recv_fd) {
    int bytes_read = 0;
#ifdef DETACHTTY_SENDFD_RECVFD
    if (recv_fd != NULL)
        bytes_read = recv_bytes_and_fd(in_fd, buf, buf_capacity, recv_fd);
    else
#endif
        bytes_read = read(in_fd, buf, buf_capacity);

    if (bytes_read > 0) {
        /*
          overwrite global variable bytes_in_buf only if we received something:
          we preserve last read buffer for whoever will attach next
        */
        bytes_in_buf = bytes_read;
        output_buffer(dribble_fd);
    }
    return bytes_read;
}

int input_buffer(int in_fd, int dribble_fd) {
  return input_buffer_recvfd(in_fd, dribble_fd, NULL);
}




int copy_a_bit_sendfd_recvfd(int in_fd, int out_fd, int dribble_fd,
                             int send_fd, int * recv_fd, char *message)
{
    /* copy whatever's available (max 4k) from in_fd to out_fd (and
       dribble_fd if !=-1)
       Return number of bytes copied. Bail if error happens.  
       Note: not re-entrant */
  
    if (input_buffer_recvfd(in_fd, dribble_fd, recv_fd) <= 0)
       return 0;
    if (output_buffer_sendfd(out_fd, send_fd) < 0) {
        if (errno != EPIPE) {
            perror(message);
            exit(1);
        }
    }
    return bytes_in_buf;
}

int copy_a_bit_sendfd_recvfd_with_log(int in_fd, int out_fd, int dribble_fd,
                                      int send_fd, int * recv_fd,
                                      char * program, char *message)
{
    int n = copy_a_bit_sendfd_recvfd(in_fd, out_fd, dribble_fd, send_fd, recv_fd, message);
    if (n==0)
        logprintf(program, "%s %s", "end-of-file while", message);
    return n;
}


int copy_a_bit(int in_fd, int out_fd, int dribble_fd, char *message) {
    return copy_a_bit_sendfd_recvfd(in_fd, out_fd, dribble_fd, -1, NULL, message);
}
int copy_a_bit_with_log(int in_fd, int out_fd,
                        int dribble_fd, char * program, char *message) {
    return copy_a_bit_sendfd_recvfd_with_log(in_fd, out_fd, dribble_fd, -1, NULL, program, message);
}

int copy_a_bit_sendfd(int in_fd, int out_fd,
                      int dribble_fd, int send_fd, char *message) {
    return copy_a_bit_sendfd_recvfd(in_fd, out_fd,
                                    dribble_fd, send_fd, NULL, message);
}
int copy_a_bit_recvfd_with_log(int in_fd, int out_fd,
                               int dribble_fd, int * recv_fd,
                               char * program, char *message) {
    return copy_a_bit_sendfd_recvfd_with_log(in_fd, out_fd,
                                             dribble_fd, -1, recv_fd,
                                             program, message);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
