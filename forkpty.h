#include <sys/termios.h>

int forkpty (int *amaster, char *name, struct termios
	     *termp, struct winsize *winp);

int daemon(int nochdir, int noclose);
