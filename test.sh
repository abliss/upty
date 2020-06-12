#!/bin/bash
set -euxo pipefail

LD_PRELOAD=$PWD/shim.so \
          strace -f -o strace.txt \
          /home/abliss/proj/gvisor/bazel-bin/test/syscalls/linux/pty_test --gtest_filter=PtyTest.SwitchNoncanonToCanonNoNewlineBig
