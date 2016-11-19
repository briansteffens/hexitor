default: build

build:
	gcc main.c -lncurses

install:
	mkdir -p ${DESTDIR}/usr/bin
	cp a.out ${DESTDIR}/usr/bin/hexitor

uninstall:
	rm ${DESTDIR}/usr/bin/hexitor

clean:
	rm a.out
