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

