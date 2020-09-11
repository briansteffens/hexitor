DESTDIR?=/usr/bin
LDIR?=/usr

default: build

build:
	gcc -Wall -I$(LDIR)/include -I$(LDIR)/include/ncurses main.c -o hexitor.o -L$(LDIR)/lib -lncurses -ltinfo 

install:
	@echo "Installing in ${DESTDIR}"
	mkdir -p ${DESTDIR}
	cp hexitor.o ${DESTDIR}/hexitor

uninstall:
	rm ${DESTDIR}/hexitor

clean:
	rm -f hexitor.o
