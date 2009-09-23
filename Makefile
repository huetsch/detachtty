
# OS_* variables vary according to your operating system.  See INSTALL
# for details

# Linux 2.4
OS_CFLAGS=-DNEED_PTY_H                       
# FreeBSD, version unknown
#OS_CFLAGS=-DNEED_LIBUTIL_H                  
# Solaris, some version of
#OS_CFLAGS=-DNEED_STRINGS_H -DNEED_LOCAL_FORKPTY_H
# MacOS X needs libpoll from fink
# OS_CFLAGS=-DNEED_LOCAL_FORKPTY_H -I/sw/include

# -lutil has forkpty() in it in Linux 2.4, and apparently at least
# doesn't break anything in FreeBSD.
OS_LOADLIBES=-lutil
# Solaris users need this instead
#OS_LOADLIBES=-lnsl -lsocket
# MacOS X with libpoll from fink
# OS_LOADLIBES=-L/sw/lib -lpoll

OS_OBJECTS=
# no forkpty in Solaris or MacOS X
# OS_OBJECTS=forkpty.o

INSTALL_DIR=/usr/local/bin

# You probably don't need to edit anything below this line

CFLAGS=-g $(OS_CFLAGS)
LOADLIBES=$(OS_LOADLIBES)

all: detachtty attachtty
clean: 
	rm -f *.o *~ attachtty detachtty

install: all
	install detachtty attachtty $(DESTDIR)$(INSTALL_DIR)

deb:
	rm -rf /usr/local/src/Packages/detachtty/ 
	CVSROOT=`cat CVS/Root` cvs-buildpackage -F -uc -us -rfakeroot
	lintian /usr/local/src/Packages/detachtty/detachtty*.changes

detachtty: detachtty.o copy-stream.o errors.o $(OS_OBJECTS)
attachtty: attachtty.o copy-stream.o errors.o $(OS_OBJECTS)
