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

#include "config.h"

#include <errno.h>
#include <sys/stat.h>
#include <pty.h>
#include <fcntl.h>
#include <string.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX    108
#endif

static char * const MY_NAME = "detachtty";

/*
  Create child process and establish the slave pseudo terminal as the
  child's controlling terminal.
  extern int forkpty __P ((int *__amaster, char *__name,
  struct termios *__termp, struct winsize *__winp));
  __amaster=fd of master pty
  __name = name of master pty, must be long enough.  how do we know?

  In the child, also dups the slave device to fds 0,1,2 and calls setsid()
  (which causes the right process group stuff to happen too)
*/

static int  parse_args(int argc, char *argv[], int * next_arg_ptr);
static void set_noecho(int fd);
static int  bind_socket_or_bail(char * socket_path);
static void usage(char *name,char *offending_option);
static void version(void);
static void init_signal_handlers(void);
static void process_accumulated_signals(void);
static void tidy_up_nicely(int signal);

/*
  1) fork and open pty pairs.  start child process on the slave end
  2) open network socket, listen to it - backlog 0
  (should be unix-domain socket)
  3) accept a single connection
*/

static char *socket_path=NULL;
static char *dribble_path=NULL;
static char *log_file_path=NULL;
static char *pid_file_path=NULL;
static int dribble_fd=-1;
static volatile int was_signaled = 0, was_sighupped = 0;

static void reopen_files(int signal) {
    if (log_file_path) {
        if (log_fp)
            fclose(log_fp);
        log_fp = fopen(log_file_path,"a");
        if (log_fp==NULL)
            perror("fopen log file");
        setvbuf(log_fp, NULL, _IONBF, 0); /* don't buffer */
        if (signal > 0)
            logprintf(MY_NAME, "Got signal %d, reopened log file \"%s\"",
                      signal, log_file_path);
    } else {
        log_fp = stderr;
    }
    if (dribble_path) {
        if (dribble_fd >= 0)
            close(dribble_fd);
        dribble_fd = open(dribble_path, O_WRONLY|O_CREAT|O_APPEND,0600);
        if (dribble_fd < 0)
            logprintf(MY_NAME,"Cannot open dribble file %s", dribble_path);
    }
}


static void usage(char *name,char *offending_option) {
    if(offending_option) 
        fprintf(stderr, "%s: unrecognized option `%s'\n",
                name,offending_option);
    else
        fprintf(stderr, "%s: unrecognized arguments\n",
                name);
    fprintf(stderr,"usage:\n"
            " %s [--version] [--no-detach] [--dribble-file name] [--log-file name] \\\n"
            "   [--pid-file name] socket-path /path/to/command [arg] [arg] [arg] ...\n",
            name);
}

static void version(void) {
    fprintf(stdout,"detachtty version %s\n%s", DETACHTTY_VERSION_STR,
            "Copyright (C) 2016-2017 Massimiliano Ghilardi\n"
            "Copyright (C) 2001-2005 Daniel Barlow\n"
            "License GPLv2+: GNU GPL version 2 or later <http://www.gnu.org/licenses/>.\n"
            "\n"
            "This is free software: you are free to change and redistribute it.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n");
}


#define CLIENT_CONNECTED (sock>=0)

int main(int argc,char *argv[], char *envp[]) {
    struct pollfd ufds[3];
    struct termios my_termios;
    struct winsize my_winsize;
    int pty_master = -1, master_socket = -1, sock = -1, send_fd = -1;
    int next_arg = 0, pid = -1, detach_p = 1;
    mode_t old_umask;

    log_fp = stderr;	/* may be changed by parse_args */

    detach_p = parse_args(argc, argv, &next_arg);

    /* this assumes we're started from a shell.  would be smart to 
       default 80x24 or something if we can't do this */
    tcgetattr(0, &my_termios);
    ioctl(0, TIOCGWINSZ, &my_winsize);

    old_umask = umask(077);
    master_socket = bind_socket_or_bail(socket_path);
    umask(old_umask);

    if (listen(master_socket, 1) != 0)
        bail(MY_NAME,"listen");

    if (detach_p) {
        int dev_null;
        if (daemon(1, 1))
            bail(MY_NAME, "daemon");
        /* we leave stderr open - if the user really wanted his tty back,
           he'd have specified --log-file */
        close(0);
        close(1);
        dev_null = open("/dev/null",O_RDWR);
        dup2(dev_null, 0);
        dup2(dev_null, 1);
    }

    if (pid_file_path) {
        FILE *fp = fopen(pid_file_path, "w");
        if (fp != NULL) {
            fprintf(fp, "%d\n", (int)getpid());
            fclose(fp);
        }
    }
    reopen_files(0);

    setlinebuf(stdout);
    setlinebuf(stderr);

    pid = forkpty(&pty_master,NULL,&my_termios,&my_winsize);
    if (pid < 0) {
        /* error */
        perror("detach: Can't fork");
        exit(1);
    } else if (pid==0) {
        /* child */
        set_noecho(0);
        execve(argv[next_arg],&argv[next_arg],envp);
        bail(MY_NAME,"detach: exec failed");
    } else {
        /* parent */

        init_signal_handlers();

        logprintf(MY_NAME, "Successfully started"); 
        for (;;) {
            ufds[0].fd=pty_master; ufds[0].events=POLLIN|POLLHUP;
            ufds[1].fd=master_socket; ufds[1].events=POLLIN;
            ufds[2].fd=sock; ufds[2].events=POLLIN|POLLHUP;

            process_accumulated_signals();

            if (poll(ufds, CLIENT_CONNECTED ? 3 : 2, -1) == -1) {
                int err = errno;
                if (err != EINTR && err != EAGAIN)
                    logprintf(MY_NAME, "poll failed: %s", strerror(errno));
                continue;
            }
            process_accumulated_signals();

            if (ufds[0].revents & POLLIN) {
                if (copy_a_bit_sendfd(pty_master,sock,dribble_fd,send_fd,"copying from child") > 0)
                    send_fd = -1;
            }
            if (ufds[0].revents & POLLHUP) {
                logprintf(MY_NAME, "child terminated, exiting");
                if (sock>=0) { close(sock); sock=-1; }
                tidy_up_nicely(0);
            }

            if (ufds[1].revents & POLLIN) {
                struct sockaddr_un their_addr;
#ifdef __linux__
                socklen_t spare_integer = sizeof(their_addr);
#else
                int spare_integer = sizeof(their_addr);
#endif
                int new_sock = accept(master_socket, (struct sockaddr *) &their_addr,
                                      &spare_integer);
                if (new_sock >= 0)
                {
                    logprintf(MY_NAME,"accepted connection%s",
                              (CLIENT_CONNECTED ? " (and closing previous one)" : ""));
                    if (CLIENT_CONNECTED)
                        close(sock);
                    sock = new_sock;
                    send_fd = pty_master;
                    /* give them a copy of anything we read recently */
                    if (output_buffer_sendfd(sock, send_fd) > 0)
                        send_fd = -1;
                    continue;
                }
            }
            if (CLIENT_CONNECTED && (ufds[2].revents & POLLIN)) {
                if (copy_a_bit_with_log(sock, pty_master, dribble_fd,
                                        MY_NAME, "copying from socket, closing connection") == 0) {
                    /* end-of-file on socket */
                    if (sock >= 0) { close(sock); sock=-1; }
                }
            }
            if (CLIENT_CONNECTED && (ufds[2].revents & POLLHUP)) {
                logprintf(MY_NAME, "closed connection due to hangup");
                close(sock); sock=-1;
            }
        }
    }
    return 0;
}

static int parse_args(int argc, char *argv[], int * next_arg_ptr) {
    int next_arg, detach_p = 1;
    for (next_arg = 1; next_arg < argc; next_arg++) {
        if (!strcmp("--no-detach", argv[next_arg])) {
            detach_p = 0;
        }
        else if (!strcmp("--dribble-file", argv[next_arg])) {
            dribble_path = argv[++next_arg];
        }
        else if (!strcmp("--log-file", argv[next_arg])) {
            log_file_path = argv[++next_arg];
        }
        else if (!strcmp("--pid-file", argv[next_arg])) {
            pid_file_path = argv[++next_arg];
        }
        else if (!strcmp("--version", argv[next_arg])) {
            version();
            exit(0);
        }
        else if (!strncmp("--", argv[next_arg], 2)) {
            usage(argv[0], argv[next_arg]);
            exit(1);
        }
        else {
            break;
        }
    }
    if (next_arg >= argc - 1) {
        usage(argv[0], NULL);
        exit(1);
    }
    socket_path=argv[next_arg++];

    if (argv[next_arg][0] != '/') {
        logprintf(MY_NAME,"\"%s\" is not an absolute path", argv[next_arg]);
        bail(MY_NAME, "argument parsing");
    }
    *next_arg_ptr = next_arg;
    return detach_p;
}

static int bind_socket_or_bail(char * socket_path) {
    char pidbuf[80];
    struct sockaddr_un addr;
    FILE * fp = NULL;
    int master_socket, oldpid = -1;

    master_socket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (master_socket < 0) {
        bail(MY_NAME, "socket");
    }
    addr.sun_family=AF_UNIX;
    strncpy(addr.sun_path, socket_path, UNIX_PATH_MAX);
    addr.sun_path[UNIX_PATH_MAX-1] = '\0';

    if (bind(master_socket, (const struct sockaddr *) &addr, sizeof(addr)) == 0) {
        return master_socket;
    }
    if (!pid_file_path) {
        goto no_pid;
    }
    fp = fopen(pid_file_path,"r");
    if (fp) {
        if (fgets(pidbuf, sizeof(pidbuf), fp)) {
            oldpid = atoi(pidbuf);
        }
        fclose(fp);
    }
    if (oldpid <= 0) {
        goto no_pid;
    }
    if (kill(oldpid,0) == 0 || errno != ESRCH) {
        logprintf(MY_NAME, "process %d for pid file \"%s\" is still running",
                  oldpid, pid_file_path);
        goto bind_failed;
    }
    /* remove socket_path and try again */
    if (unlink(pid_file_path) != 0) {
        goto bind_failed;
    }
    logprintf(MY_NAME, "found and removed stale socket \"%s\" from a previous run",
              socket_path);

    if (bind(master_socket, (const struct sockaddr *) &addr, sizeof(addr)) == 0) {
        return master_socket;
    } else {
        goto bind_failed;
    }

 no_pid:
    logprintf(MY_NAME, "Cannot create \"%s\": does it already exist from a previous run?",
              socket_path);
 bind_failed:
    if (master_socket >= 0)
        close(master_socket);

    bail(MY_NAME, "bind");
    return -1;
}


/* borrowed from APUE example code found at
   http://www.yendor.com/programming/unix/apue/pty/main.c
*/

/* turn off echo (for slave pty) */
static void set_noecho(int fd) {
#ifdef NEED_SET_NOECHO
    struct termios  stermios;

    if (tcgetattr(fd, &stermios) < 0)
        bail("detach child","tcgetattr error");

    stermios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    stermios.c_lflag|=ICANON;
    /* stermios.c_oflag &= ~(ONLCR); */
    /* would also turn off NL to CR/NL mapping on output */
    stermios.c_cc[VERASE]=0177;
#ifdef VERASE2
    stermios.c_cc[VERASE2]=0177;
#endif
    if (tcsetattr(fd, TCSANOW, &stermios) < 0)
        bail("detach child","tcsetattr error");
#else
    (void)fd;
#endif /* NEED_SET_NOECHO */
}

static void cleanup_signal_handler(int sig) {
    struct sigaction act;
    act.sa_handler = SIG_DFL;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = 0;
    sigaction(sig, &act, 0);
}

static void fatal_signal_handler(int sig) {
    was_signaled = sig;
    cleanup_signal_handler(sig);
}

static void sighup_signal_handler(int sig) {
    was_sighupped = sig;
}

static void init_signal_handlers(void) {
    struct  sigaction act;
    int fatal_sig[] = {
#ifdef SIGHUP
        SIGHUP,
#endif
#ifdef SIGQUIT
        SIGQUIT,
#endif
#ifdef SIGILL
        SIGILL,
#endif
#ifdef SIGABRT
        SIGABRT,
#endif
#ifdef SIGBUS
        SIGBUS,
#endif
#ifdef SIGFPE
        SIGFPE,
#endif
#ifdef SIGSEGV
        SIGSEGV,
#endif
#ifdef SIGPIPE
        /*SIGPIPE,*/
#endif
#ifdef SIGTERM
        SIGTERM,
#endif
#ifdef SIGSTKFLT
        SIGSTKFLT,
#endif
#ifdef SIGCHLD
        SIGCHLD,
#endif
#ifdef SIGXCPU
        SIGXCPU,
#endif
#ifdef SIGXFSZ
        SIGXFSZ,
#endif
    };
    unsigned i;

    /* catch SIGCHLD, SIGQUIT, SIGTERM, SIGILL, SIGFPE... and exit */
    act.sa_handler = fatal_signal_handler;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = SA_RESETHAND;
    for (i = 0; i < sizeof(fatal_sig)/sizeof(fatal_sig[0]); i++) {
        sigaction(fatal_sig[i],&act,0);
    }

    /* we can be HUPped */
    act.sa_handler = sighup_signal_handler;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = 0;
    sigaction(SIGHUP, &act,0);

    /* ignore SIGPIPE */
    act.sa_handler = SIG_IGN;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = 0;
    sigaction(SIGPIPE, &act,0);
}

static void process_accumulated_signals(void) {
    if (was_signaled) {
        tidy_up_nicely(was_signaled);
        was_signaled = 0;
    }
    if (was_sighupped) {
        reopen_files(was_sighupped);
        was_sighupped = 0;
    }
}

static void tidy_up_nicely(int signal) {
    if (signal)
        logprintf(MY_NAME, "got unexpected signal %d, exiting", signal);
    else
        logprintf(MY_NAME, "exiting", signal);
    if (unlink(socket_path))
        logprintf(MY_NAME, "error unlinking \"%s\": %s", socket_path, strerror(errno));
    if (pid_file_path && unlink(pid_file_path) != 0)
        logprintf(MY_NAME, "error unlinking \"%s\": %s", pid_file_path, strerror(errno));
    exit(signal);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
