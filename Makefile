.PHONY: test
all: shim.so

SOURCES=$(shell find glibc -name '*.c')
HEADERS=$(shell find glibc -name '*.h')
shim.so: shim.c ${SOURCES} ${HEADERS}
	gcc -g -shared -fPIC -o shim.so -Wl,--no-as-needed -ldl shim.c ${SOURCES}

test: shim.so
	./test.sh
