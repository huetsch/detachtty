#include "config.h"

#include <sys/stat.h>
#include <fcntl.h>
 
#define UNIX_PATH_MAX    108

#define MY_NAME "detachtty"

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

static void set_noecho(int fd);
int copy_a_bit(int in_fd, int out_fd, int dribble_fd,char *message) ;
int logprintf(char *progname, char *msg,...) ;
void usage(char *name,char *offending_option);
/*
  1) fork and open pty pairs.  start child process on the slave end
  2) open network socket, listen to it - backlog 0
  (should be unix-domain socket)
  3) accept a single connection

*/

char *socket_path=NULL;
char *dribble_path=NULL;
char *log_file_path=NULL;
char *pid_file_path=NULL;
int dribble_fd=-1;

void tidy_up_nicely(int signal) {
    if(unlink(socket_path)) bail("detachtty","unlinking %s",socket_path);
    if((pid_file_path!=0) && unlink(pid_file_path)) perror(pid_file_path);
    if(signal)
        logprintf(MY_NAME,"got unexpected signal %d",signal);
    else
        logprintf(MY_NAME,"exiting",signal);
    exit(signal);
}

extern FILE *log_fp;
void open_files(int signal) {
    if(log_file_path) {
        if(log_fp) fclose(log_fp);
        log_fp=fopen(log_file_path,"a");
        if(log_fp==NULL) perror("fopen");
        setvbuf(log_fp,NULL,_IONBF,0); /* don't buffer */
        if(signal>0) 
            logprintf(MY_NAME,"Got signal %d, reopened log file\n",signal);
    } else {
        log_fp=stderr;
    }
    if(dribble_path) {
        if(dribble_fd>=-1) close(dribble_fd);
        dribble_fd=open(dribble_path,O_WRONLY|O_CREAT|O_APPEND,0600);
        if(dribble_fd==-1) 
            logprintf(MY_NAME,"Cannot open dribble file %s",dribble_path);
    }
}

#define CLIENT_CONNECTED (sock>=0)

main(int argc,char *argv[], char *envp[]) {
    int pty_master,pty_slave;
    char pty_name[]="/dev/pts/wherever/something"; /* XXX */
    int master_socket=-1,sock=-1,next_arg;
    struct pollfd ufds[3];
    struct sockaddr_un s_a,their_addr;
    int spare_integer=1;
    mode_t old_umask;

    struct termios my_termios;
    struct winsize my_winsize;
    int pid;

    /* command line options */
    int detach_p=1;
    log_fp=stderr;		/* may  be changed later */
  
    for(next_arg=1;next_arg<argc;next_arg++) {
        if(!strcmp("--no-detach",argv[next_arg])) { detach_p=0; continue; }
        if(!strcmp("--dribble-file",argv[next_arg])) {
            dribble_path=strdup(argv[++next_arg]); continue;
        }
        if(!strcmp("--log-file",argv[next_arg])) {
            log_file_path=strdup(argv[++next_arg]); continue;
        }
        if(!strcmp("--pid-file",argv[next_arg])) {
            pid_file_path=strdup(argv[++next_arg]); continue;
        }
        if(!strncmp("--",argv[next_arg],2)) {
            usage(argv[0],argv[next_arg]);
            exit(1);
        }
        break;
    }
    if(next_arg>=(argc-1)) {
        usage(argv[0],0L);
        exit(1);
    }
    socket_path=argv[next_arg++];

    if(argv[next_arg][0]!='/') {
        logprintf(MY_NAME,"\"%s\" is not an absolute path",argv[next_arg]);
        bail(MY_NAME,"argument parsing");
    }


    /* this assumes we're started from a shell.  would be smart to 
       default 80x24 or something if we can't do this */
    tcgetattr(0,&my_termios);
    ioctl(0,TIOCGWINSZ,&my_winsize);
  
    s_a.sun_family=AF_UNIX;
    strncpy(s_a.sun_path,socket_path,UNIX_PATH_MAX);
    s_a.sun_path[UNIX_PATH_MAX-1]='\0';
  
    old_umask=umask(077);
    master_socket=socket(PF_UNIX,SOCK_STREAM,0);
    if(master_socket==-1) bail(argv[0],"socket");

    spare_integer=1;
    if(bind(master_socket,(const struct sockaddr *) &s_a,sizeof s_a)!=0) {
        logprintf(MY_NAME,"Is \"%s\" a dead socket from a previous run?\n",socket_path);
        bail(MY_NAME,"bind");
    }
    if(listen(master_socket,1)!=0) bail(MY_NAME,"listen");
    if(detach_p) {
        if(daemon(1,1)) bail(MY_NAME, "daemon");
        /* we leave stderr open - if the user really wanted his tty back,
           he'd have specified --log-file */
        close(0); close(1);
        open("/dev/null",O_RDONLY);
        dup(0);
    }    
  
    if(pid_file_path) {
        FILE *fp=fopen(pid_file_path,"w");
        fprintf(fp,"%d\n",getpid());
        fclose(fp);
    }
    open_files(0);

    pid=forkpty(&pty_master,pty_name,&my_termios,&my_winsize);
    if(pid<0) {			/* error */
        perror("detach: Can't fork");
        exit(1);
    } else if(pid==0) {
        /* child */
        umask(old_umask);
        set_noecho(0);
        execve(argv[next_arg],&argv[next_arg],envp);
        bail(MY_NAME,"detach: exec failed");
    } else {			/* parent */

        struct  sigaction  act;
        act.sa_handler=tidy_up_nicely;
        sigemptyset(&(act.sa_mask));
        act.sa_flags=SA_RESETHAND;
        sigaction(SIGINT,&act,0);
        sigaction(SIGQUIT,&act,0);
        sigaction(SIGSEGV,&act,0);
        sigaction(SIGBUS,&act,0);
        sigaction(SIGTERM,&act,0);
        act.sa_handler=open_files; /* we can be HUPped */
        sigemptyset(&(act.sa_mask));
        act.sa_flags=0;
        sigaction(SIGHUP,&act,0);

        logprintf(MY_NAME,"Successfully started\n"); 
        while(1) {
            ufds[0].fd=pty_master; ufds[0].events=POLLIN|POLLHUP;
            ufds[1].fd=master_socket; ufds[1].events=POLLIN;
            ufds[2].fd=sock; ufds[2].events=POLLIN|POLLHUP;
            if(poll(ufds,CLIENT_CONNECTED ? 3 : 2 ,-1) == -1) {
                logprintf(MY_NAME, "poll returned -1\n");
                continue;
            }
            if(ufds[0].revents & POLLIN) 
                copy_a_bit(pty_master,sock,dribble_fd,"copying from pty");
      
            if(ufds[1].revents & POLLIN) {
                spare_integer=sizeof their_addr;
                sock=accept(master_socket,(struct sockaddr *) &their_addr,
                            &spare_integer);
                logprintf(MY_NAME,"accepted connection");
                /* give them a copy of anything we read recently */
                output_buffer(sock);
                continue;
            }
            if(CLIENT_CONNECTED && (ufds[2].revents & POLLIN)) {	
                int n=copy_a_bit(sock,pty_master,dribble_fd,"copying to pty");
                if(n==0) {
                    logprintf(MY_NAME,"closed connection due to zero-length read");
                    if(sock>-1) { close(sock); sock=-1; }
                }
            }
            if(ufds[0].revents & POLLHUP) {
                logprintf(MY_NAME,"Child terminated, exiting\n");
                if(sock>-1) { close(sock); sock=-1; }
                tidy_up_nicely(0);
            }
            if(CLIENT_CONNECTED && (ufds[2].revents & POLLHUP)) {
                logprintf(MY_NAME,"closed connection due to hangup");
                close(sock); sock=-1;
            }
        }
    }
}

/* borrowed from APUE example code found at
   http://www.yendor.com/programming/unix/apue/pty/main.c
*/

static void
set_noecho(int fd)              /* turn off echo (for slave pty) */
{
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
        bail("tcsetattr error");
}

void usage(char *name,char *offending_option) {
    if(offending_option) 
        fprintf(stderr,"%s: unrecognized option `%s'\n",
                name,offending_option);
    else
        fprintf(stderr,"%s: unrecognized arguments\n",
                name);
    fprintf(stderr,"usage: \n %s [--no-detach] [--dribble-file name] [--log-file name] \\\n   [--pid-file name] socket-path /path/to/command [arg] [arg] [arg] ...\n", name);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
