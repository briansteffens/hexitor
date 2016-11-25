default: build

build:
	gcc -Wall main.c -lncurses

install:
	mkdir -p ${DESTDIR}/usr/bin
	cp a.out ${DESTDIR}/usr/bin/hexitor

uninstall:
	rm ${DESTDIR}/usr/bin/hexitor

clean:
	rm -f a.out
