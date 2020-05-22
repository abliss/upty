#!/bin/bash
LD_PRELOAD=$PWD/shim.so \
          strace -f -o strace.txt \
	      script --return --command \
          "python -c 'import os;exit(0 if os.isatty(1) else 1)'"
