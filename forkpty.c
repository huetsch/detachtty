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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <stropts.h>

int forkpty (int *amaster, char *name, struct termios
	     *termp, struct winsize *winp)
{
    int fdm, fds;
    char *slavename;
    pid_t pid;
    fdm = open("/dev/ptmx", O_RDWR);  /* open master */
    grantpt(fdm);                     /* change permission of slave */
    unlockpt(fdm);                    /* unlock slave */
    slavename = ptsname(fdm);         /* get name of slave */
    if (name) strcpy(name, slavename);
    *amaster = fdm;
    if ((pid = fork()) < 0) {
	return pid; 		/* error */
    }
    else if (pid != 0) {		/* parent */
	return pid;
    }
    else {			/* child */
	pid_t pgid;
	/* create a new session */
	pgid = setsid();
	if (pgid == -1) {
	    perror("setsid failed");
	    return -1;
	}
	fds = open(slavename, O_RDWR);    /* open slave */
	ioctl(fds, I_PUSH, "ptem");       /* push ptem */
	ioctl(fds, I_PUSH, "ldterm");    /* push ldterm */
	dup2(fds, 0);
	dup2(fds, 1);
	dup2(fds, 2);
	ioctl(fds, TIOCSPGRP, &pgid);
	/* magic */
	if (termp)
	    ioctl(fds, TCSETS, termp);
	if (winp)
	    ioctl(fds, TIOCSWINSZ, winp);
        if (fds > 2)
            close(fds);
	return pid;
    }
}
      

int daemon(int nochdir, int noclose)
{
    int fd;

    switch (fork()) {
    case -1:
	return (-1);
    case 0:
	break;
    default:
	exit(0);
    }

    if (setsid() == -1)
	return (-1);

    if (!nochdir)
	chdir("/");

    if (!noclose && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2)
	    close (fd);
    }
    return (0);
}

