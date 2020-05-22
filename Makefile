.PHONY: test
all: shim.so

shim.so: shim.c
	gcc -shared -fPIC -o shim.so -Wl,--no-as-needed -ldl shim.c $(find glibc -name '*.c')

test: shim.so
	./test.sh
