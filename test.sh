#!/bin/bash
set -euxo pipefail

LD_PRELOAD=$PWD/shim.so \
          strace -f -o strace.txt \
	      script --return --command \
          "python -c 'import os;exit(0 if os.isatty(1) else 1)'"

LD_PRELOAD=$PWD/shim.so \
          strace -f -o strace.txt \
	      script --return --command \
          "tty"
LD_PRELOAD=$PWD/shim.so \
          strace -f -o strace.txt \
	      script --return --command \
          "/bin/stty"
