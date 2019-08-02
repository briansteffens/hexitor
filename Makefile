DESTDIR?=/usr/bin

default: build

build:
	gcc -Wall main.c -lncurses -ltinfo -o hexitor.o

install:
	@echo "Installing in ${DESTDIR}"
	mkdir -p ${DESTDIR}
	cp hexitor.o ${DESTDIR}/hexitor

uninstall:
	rm ${DESTDIR}/hexitor

clean:
	rm -f hexitor.o
