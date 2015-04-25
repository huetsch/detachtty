#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

FILE *log_fp=NULL;

int logprintf(char *progname, char *msg,...) {
    int n;
    va_list ap;
    va_start(ap, msg);
    fprintf(log_fp,";;; %s: %ld: ",progname,time(0));
    n=vfprintf(log_fp,msg,ap);
    va_end(ap);
    fputs("\r\n",log_fp);
    return n;
}

void bail(char *progname,char *msg,...) {
    va_list ap;
    int e=errno;
    va_start(ap, msg);
    fprintf(log_fp,";;; %s: %ld: FATAL ",progname,time(0));
    vfprintf(log_fp,msg,ap);
    va_end(ap);
    /* use \r\n to avoid staircase effect */
    if(e>0) 
	fprintf(log_fp," (%s)\r\n",strerror(e));
    else
	fputs("\r\n",log_fp);

    kill(getpid(),15);
}

