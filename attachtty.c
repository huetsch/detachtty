#include "config.h"
#include "time.h"

extern FILE *log_fp;
int copy_a_bit(int in_fd, int out_fd, int dribble_fd,char *message) ;
void connect_direct(char * path, char *cmd, int timeout) ;
void connect_ssh(char *host, char *path, char *cmd) ;

int init_tty(void);
int cleanup_tty(void);
void setup_ctrl_z_handler(void);

#define UNIX_PATH_MAX 108

/*
  $0 /tmp/my-socket
 
  or

  $0 user@hostname:/tmp/my-socket

  In the latter case, we open an ssh connection to user@hostname then
  run $0 on the remote machine with the remainder of the command

*/

int was_interrupted=0, time_to_die=0;
void tears_in_the_rain(int signal) {
    time_to_die=signal;
}
void control_c_pressed(int signal) {
    was_interrupted=1;
}
void control_z_pressed(int signal) {
    /* restore tty settings before suspending ourself */
    cleanup_tty();
    
    raise(signal);

    /* received SIGCONT: perform again initial setup */
    setup_ctrl_z_handler();
    init_tty();
}

void setup_ctrl_z_handler(void)  {
    struct  sigaction act;
    act.sa_handler=control_z_pressed;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=SA_RESETHAND;
    sigaction(SIGTSTP,&act,0);
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
    char *cmd=NULL;
    int timeout = 1;
    struct  sigaction  act;
  
    if(argc<2 || argc>4) {
	    fprintf(stderr, "%s: unrecognized arguments\nusage: %s /path/to/socket [cmd] [timeout]\n       %s remote_user@remote_host:/path/to/socket [cmd]\n", argv[0],  argv[0], argv[0]);
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
        cmd = argv[2];
    if (argc == 4) {
        int read_timeout = atoi(argv[3]);
        if (read_timeout > 0)
            timeout = read_timeout;
    }

    /* catch SIGINT and send character \003 over the link */
    act.sa_handler=control_c_pressed;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=0;
    sigaction(SIGINT,&act,0);
    /* catch SIGCHLD or SIGQUIT and exit */
    act.sa_handler=tears_in_the_rain;
    sigemptyset(&(act.sa_mask));
    act.sa_flags=SA_RESETHAND;
    sigaction(SIGCHLD,&act,0);
    sigaction(SIGQUIT,&act,0);
    /* catch SIGSTOP and cleanup tty */
    setup_ctrl_z_handler();

    if (host) {
        logprintf("attachtty","connecting through ssh to %s on %s\n",path,host);
        connect_ssh(host,path,cmd);
    } else {
        logprintf("attachtty","connecting directly to %s\n",path);
        init_tty();
        connect_direct(path,cmd,timeout);
        cleanup_tty();
    }
    if (time_to_die != 0) {
        logprintf("attachtty","got signal %d, closing down\n",time_to_die);
    }
    return 0;
}

/* copy between stdin,stdout and unix-domain socket */

void connect_direct(char * path, char *cmd, int timeout) {
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
  
    if (cmd) {
        int time_start = time(NULL);
        int time_end = time_start + timeout;
        ufds[0].fd=sock; ufds[0].events=POLLIN | POLLOUT;

        while(! time_to_die) {
            if(was_interrupted) {
                write(sock,"\003",1);
                was_interrupted=0;
            }
            int msec_left = (time_end - time(NULL)) * 1000;  
            if (poll(ufds, 1 , msec_left) == -1) continue;
            if(ufds[0].revents & POLLIN) 
                copy_a_bit(sock,1,-1,"copying from socket");
	
            if(cmd && (ufds[0].revents & POLLOUT)) {
                int len = strlen(cmd);
                int written = write(sock,cmd,strlen(cmd));
                if (written == len) {
                    ufds[0].events = POLLIN; /* no longer need to output */
                    cmd = NULL;
                    write(sock,"\012",1);
                } else 
                    cmd += written;
            }

            if (time(NULL) >= time_end) {
                close(sock);
                sock=-1;
                time_to_die = 1;
            }
        }
    } else {
        while(! time_to_die) {
            if(was_interrupted) {
                write(sock,"\003",1);
                was_interrupted=0;
            }
            ufds[0].fd=sock; ufds[0].events=POLLIN;
            ufds[1].fd=0; ufds[1].events=POLLIN|POLLHUP;
            if (poll(ufds,2 ,-1) == -1) continue;
            if(ufds[0].revents & POLLIN) 
                copy_a_bit(sock,1,-1,"copying from socket");
    
            if(ufds[1].revents & POLLIN) {	
                int n=copy_a_bit(0,sock,-1,"copying to socket");
                if(n==0) {
                    logprintf("attachtty","closed connection due to zero-length read");
                    close(sock); sock=-1;
                }
            }
            if(ufds[1].revents & POLLHUP) {
                logprintf("attachtty","closed connection due to hangup");
                exit(0);
            }
        }
    }
}



void connect_ssh(char *host, char *path, char *cmd) {
    /* 
     * ssh option -t forces tty allocation on remote side.
     * Needed to properly setup local and remote tty,
     * including -icanon -iecho flags and forwarding of CTRL+C
     */
    if (cmd) {
        execlp("ssh", "ssh", "-t", host, "attachtty", path, cmd, (char *)NULL);
    } else {
        execlp("ssh", "ssh", "-t", host, "attachtty", path,  (char *)NULL);
    }
    bail("attachtty", "exec ssh failed");
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
