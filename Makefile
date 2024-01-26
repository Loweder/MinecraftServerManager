VERSION = 2.0.0

PREFIX = /usr/local

LIBS = -lzip -lreadline 

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DMSERMAN_VERSION=\"${VERSION}\"
CFLAGS   = -std=c++2b -pedantic -Wall -Wno-deprecated-declarations -O3 ${CPPFLAGS}
LDFLAGS  = ${LIBS}

CC = g++

SRC = jsoncpp.cpp verify.cpp utility.cpp operation.cpp info.cpp main.cpp interactive.cpp
OBJ = ${SRC:.cpp=.o}

mserman: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

%.o: %.cpp
	${CC} -c ${CFLAGS} -o $@ $<

clean:
	rm -f mserman ${OBJ}

absolute: clean install

install: mserman
	mkdir -p ${PREFIX}/bin
	cp -f mserman ${PREFIX}/bin
	chmod 755 ${PREFIX}/bin/mserman

uninstall:
	rm -f ${PREFIX}/bin/mserman

.PHONY: clean install uninstall absolute

