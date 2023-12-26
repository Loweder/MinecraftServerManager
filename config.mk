VERSION = 2.0.0

PREFIX = /usr/local

LIBS = -lzip 

CFLAGS   = -std=c++2b -pedantic -Wall -Wno-deprecated-declarations -O3
CPPFLAGS = ${CFLAGS} -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DMSERMAN_VERSION=\"${VERSION}\"
#CFLAGS   = -g -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
LDFLAGS  = ${LIBS}

CC = g++
