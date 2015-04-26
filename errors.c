#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

FILE *log_fp = NULL;

int logprintf(char *progname, char *format,...) {
    va_list ap;
    int n;
    va_start(ap, format);
    fprintf(log_fp, ";;; %s: %ld: ", progname, time(NULL));
    n=vfprintf(log_fp, format, ap);
    va_end(ap);
    fputs("\r\n",log_fp);
    return n;
}

void bail(char *progname,char *format,...) {
    va_list ap;
    int err = errno;
    va_start(ap, format);
    fprintf(log_fp,";;; %s: %ld: FATAL ", progname, time(NULL));
    vfprintf(log_fp, format, ap);
    va_end(ap);
    /* use \r\n to avoid staircase effect */
    if (err > 0) 
	fprintf(log_fp, " (%s)\r\n", strerror(err));
    else
	fputs("\r\n",log_fp);

    kill(getpid(), SIGTERM);
}

