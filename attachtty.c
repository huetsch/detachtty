#include <errno.h>
#include <time.h>

#include "config.h"

extern FILE *log_fp;
void connect_direct(char * path, char *text, int timeout) ;
void connect_ssh(char *host, char *path, char *text, char *timeout) ;

int init_tty(void);
int cleanup_tty(void);
void init_signal_handlers(void);
void cleanup_signal_handler(int signal);

#define UNIX_PATH_MAX 108

/*
  $0 /tmp/my-socket
 
  or

  $0 user@hostname:/tmp/my-socket

  In the latter case, we open an ssh connection to user@hostname then
  run $0 on the remote machine with the remainder of the command

*/

volatile int was_interrupted=0, was_suspended=0, time_to_die=0;
void tears_in_the_rain(int signal) {
    time_to_die=signal;
    cleanup_signal_handler(signal);
}
void control_c_pressed(int signal) {
    was_interrupted=1;
}
void control_z_pressed(int signal) {
    was_suspended=1;
}

void init_ctrl_z_handler(void)  {
    struct sigaction act;
    act.sa_handler = control_z_pressed;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = SA_RESETHAND;
    sigaction(SIGTSTP,&act,0);
}

void cleanup_ctrl_z_handler(void) {
    cleanup_signal_handler(SIGTSTP);
}

void suspend_myself(void) {
    /* restore tty and SIGTSTP settings before suspending myself */
    cleanup_tty();
    cleanup_ctrl_z_handler();
    
    kill(getpid(), SIGTSTP);

    /* received SIGCONT: perform again initial setup */
    init_ctrl_z_handler();
    init_tty();
}


void init_signal_handlers(void) {
    struct sigaction act;
    int i, fatal_sig[] = { SIGHUP, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGTERM, SIGSTKFLT, SIGCHLD, SIGXCPU, SIGXFSZ, };
    
    /* catch SIGINT and send character \003 over the link */
    act.sa_handler=control_c_pressed;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=0;
    sigaction(SIGINT,&act,0);

    /* catch SIGSTOP and cleanup tty before suspending */
    init_ctrl_z_handler();

    /* catch SIGCHLD, SIGQUIT, SIGTERM, SIGILL, SIGFPE... and exit */
    act.sa_handler = tears_in_the_rain;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=SA_RESETHAND;
    for (i = 0; i < sizeof(fatal_sig)/sizeof(fatal_sig[0]); i++) {
        sigaction(fatal_sig[i],&act,0);
    }
}

void cleanup_signal_handler(int sig) {
    struct sigaction act;
    act.sa_handler=SIG_DFL;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=0;
    sigaction(sig,&act,0);
}



   

struct termios saved_tty;

#ifndef _POSIX_VDISABLE
# define _POSIX_VDISABLE 0
#endif
#ifndef ONOCR
# define ONOCR 0
#endif

int init_tty(void) {
    struct termios tty;
    int err = tcgetattr(0, &saved_tty);
    if (err == 0)
    {
        tty = saved_tty;
        tty.c_iflag &= ~(INLCR|ICRNL|IGNCR|IXON|IXOFF);
        tty.c_oflag &= ~(ONLCR|OCRNL|ONOCR|ONLRET);
        tty.c_lflag &= ~(ECHO|ICANON|IEXTEN);
        tty.c_cc[VSTART] = tty.c_cc[VSTOP] = _POSIX_VDISABLE;
        err = tcsetattr(0, TCSADRAIN, &tty);
    }
    return err;
}

int cleanup_tty(void) {
    return tcsetattr(0, TCSADRAIN, &saved_tty);
}

int main(int argc,char *argv[], char *envp[]) {
    char *host=NULL;		/* "hostname" or "user@hostname" */
    char *path;			/* path to socket */
    char *p;
    char *text=NULL;
    char *timeout_str=NULL;
    int timeout = 1;
  
    if (argc<2 || argc>4) {
        fprintf(stderr,
		"%s: unrecognized arguments\n"
		"usage: %s /path/to/socket [text] [timeout]\n"
		"       %s remote_user@remote_host:/path/to/socket [text] [timeout]\n",
		argv[0],  argv[0], argv[0]);
        exit(1);
    }
    p=strdup(argv[1]);
    log_fp=stderr;
    if((path=strchr(p,':')) != NULL) {
	    host=p;
	    *path='\0';
	    path++;
    } else {
	    path=p;
    }
    if (argc >= 3)
        text = argv[2];
    if (argc == 4) {
        timeout_str = argv[3];
        int read_timeout = atoi(timeout_str);
        if (read_timeout > 0)
            timeout = read_timeout;
    }

    init_signal_handlers();

    if (host) {
        logprintf("attachtty","connecting through ssh to %s on %s\n",path,host);
        connect_ssh(host,path,text,timeout_str);
    } else {
        logprintf("attachtty","connecting directly to %s\n",path);
        init_tty();
        connect_direct(path,text,timeout);
        cleanup_tty();
    }
    if (time_to_die != 0) {
        logprintf("attachtty","got signal %d, closing down\n",time_to_die);
    }
    return 0;
}

/* copy between stdin,stdout and unix-domain socket */

void connect_direct(char * path, char *text, int timeout) {
    int sock=-1;
    struct pollfd ufds[3];
    struct sockaddr_un s_a;

    s_a.sun_family=AF_UNIX;
    strncpy(s_a.sun_path,path,UNIX_PATH_MAX);
    s_a.sun_path[UNIX_PATH_MAX-1]='\0';
  
    sock=socket(PF_UNIX,SOCK_STREAM,0);
    if(sock==-1) bail("attachtty","socket");
  
    if(connect(sock,(const struct sockaddr *) &s_a,sizeof s_a)!=0) 
	    bail("attachtty","connect");
  
    if (text) {
        int time_start = time(NULL);
        int time_end = time_start + timeout;
        int msec_left;
        ufds[0].fd=sock; ufds[0].events=POLLIN | POLLOUT;

        while(! time_to_die) {
            if (was_interrupted) {
                was_interrupted=0;
                write(sock,"\003",1);
            }
	    if (was_suspended) {
                was_suspended=0;
	        suspend_myself();
	    }
            msec_left = (time_end - time(NULL)) * 1000;  
            if (poll(ufds, 1, msec_left) == -1) {
	       if (errno == EINTR)
		   continue;
	       else
		   bail("attachtty", "poll");
	    }
	        
            if (ufds[0].revents & POLLIN) {
                if (copy_a_bit_with_log(sock,1,-1,"attachtty","copying from socket") == 0)
                    break;
            }
	
            if (text && (ufds[0].revents & POLLOUT)) {
                int len = strlen(text);
                int written = write(sock,text,len);
                if (written == len) {
                    ufds[0].events = POLLIN; /* no longer need to output */
                    text = NULL;
                    write(sock,"\r",1);
                } else 
                    text += written;
            }

            if (time(NULL) >= time_end) {
                close(sock);
                sock=-1;
                time_to_die = 1;
            }
        }
    } else {
        while(! time_to_die) {
            if (was_interrupted) {
                write(sock,"\003",1);
                was_interrupted=0;
            }
	    if (was_suspended) {
                was_suspended=0;
	        suspend_myself();
	    }
            ufds[0].fd=sock; ufds[0].events=POLLIN;
            ufds[1].fd=0; ufds[1].events=POLLIN|POLLHUP;
            if (poll(ufds, 2, -1) == -1) {
	       if (errno == EINTR)
		   continue;
	       else
		   bail("attachtty", "poll");
	    }
	   
	    if (ufds[0].revents & POLLIN) {
                if (copy_a_bit_with_log(sock,1,-1,"attachtty","copying from socket") == 0)
                    break;
            }
            if (ufds[1].revents & POLLIN) {	
                if (copy_a_bit_with_log(0,sock,-1,"attachtty","copying to socket") == 0)
                    break;
            }
            if (ufds[1].revents & POLLHUP) {
                logprintf("attachtty","closed connection due to hangup");
                break;
            }
        }
    }
}



void connect_ssh(char *host, char *path, char *text, char *timeout_str) {
    /* 
     * ssh option -t forces tty allocation on remote side.
     * Needed to properly setup local and remote tty,
     * including -icanon -iecho flags and forwarding of CTRL+C
     */
    execlp("ssh", "ssh", "-t", host, "attachtty", path, text, timeout_str, (char *)NULL);
    bail("attachtty", "exec ssh failed");
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
