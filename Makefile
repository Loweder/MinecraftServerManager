# See LICENSE file for copyright and license details.

include config.mk

SRC = jsoncpp.cpp verify.cpp utility.cpp operation.cpp info.cpp main.cpp
OBJ = ${SRC:.cpp=.o}

all: options mserman

options:
	@echo mserman build options:
	@echo "CFLAGS   = ${CPPFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "C++      = ${CC}"

.c.o:
	${CC} -c ${CPPFLAGS} $<

${OBJ}: config.mk

mserman: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f mserman ${OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f mserman ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/mserman

absolute: clean install

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/mserman

.PHONY: all options clean dist install uninstall absolute

