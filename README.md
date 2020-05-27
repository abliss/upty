upty - Userspace pseudoterminal

The goal of upty is to provide a functional replacement, purely in userspace,
for the kernel's devpts filesystem.

This is a hack-in-progress; it doesn't really work yet.

To try it out, update the submodules and then run `make test`.

The idea is to use an `LD_PRELOAD` shim to intercept calls to pty-related glibc
functions (as well as `open` and `ioctl` for non-portable programs which use
those directly). Instead of opening the real `/dev/ptmx` to ask the kernel for a
new "master" pseudoterminal, we connect to a unix domain socket. The other end
of that socket is a daemon (written in golang) handles all the line discipline
and terminal settings (using code from gvisor, which is not wired up yet), and
shuttle bytes between a second socket which will stand in for the corresponding
"slave" terminal.

Here are some test programs which use pseudoterminals:

[  X  ] `script`
[  X  ] `tmux`
[  X  ] `ttyd`
[01/58] `gvisor` test case `//test/syscalls/linux:pty_test`
[     ] `screen` (only non-setuid mode is contemplated)
[     ] `emacs`
[     ] `xterm`
[     ] `rxvt`
[     ] `asciinema`
[     ] Android Terminal