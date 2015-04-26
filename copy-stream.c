#include "config.h"

static char buf[4097];
static int bytes_in_buf=0;

int copy_a_bit(int in_fd, int out_fd, int dribble_fd, char *message) {
    /* copy whatever's available (max 4k) from in_fd to out_fd (and
       dribble_fd if !=-1)
       Return number of bytes copied. Bail if error happens.  
       Note: not re-entrant */
  
    bytes_in_buf=read(in_fd,buf,4096);
    if(!bytes_in_buf) return 0;
    output_buffer(dribble_fd);
    if(output_buffer(out_fd)==-1) {
	perror(message);
	exit(1);
    }
    return bytes_in_buf;
}

int output_buffer(int fd) {
    int bytes_written=0,bytes_to_write=bytes_in_buf;
    if(fd<0) return;
    while(bytes_to_write>0) {
	bytes_written=write(fd,buf,bytes_to_write);
	if(bytes_written==-1) return -1;
	bytes_to_write-=bytes_written;
    }
    return bytes_in_buf;
}
