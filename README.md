# upty - Userspace pseudoterminal

The goal of upty is to provide a functional replacement, purely in userspace,
for the kernel's devpts filesystem.

# Status
It kinda works. There's no job contol yet, so e.g. Ctrl-C and Ctrl-Z won't be
translated to signals.

# How to use
To try it out:
1. Fetch the submodules: `git submodule update --init --depth 1`
2. Start the backend daemon in your homedir: 
   `go build main.go && (P=$PWD;cd;$P/main&)`
3. Build the `shim.so`: `make`.
4. Run a simple test with `script` and python: `make test`
5. Run your favorite pty-using binary: `LD_PRELOAD=$PWD/shim.so tmux`

# Design
The idea is to use an `LD_PRELOAD` shim to intercept calls to pty-related glibc
functions (as well as `open` and `ioctl` for non-portable programs which use
those directly). Instead of opening the real `/dev/ptmx` to ask the kernel for a
new "master" pseudoterminal, we connect to a unix domain socket. On the other
end of that connection is a daemon (written in golang) which handles all the
line discipline and terminal settings (using code from gvisor), and shuttles
bytes to/from a second connection which will stand in for the corresponding
"slave" terminal.

Binaries which do syscalls directly, or are statically linked, won't work. Nor
will setuid binaries (like `screen`). Perhaps someday we can design a better API
than the half-century-old protocol for talking to teletype machines over serial
lines; then perhaps these binaries can be recompiled to speak that API directly
to the `upty` daemon.

# Roadmap
Here are some test programs which use pseudoterminals; perhaps someday they will
work with upty. Right now, an `x` means that I've tried them and they seem to
pretty much mostly work.

- [x] `script`
- [x] `tmux`
- [x] `ttyd` (with patches)
- [35/58] `gvisor` test case `//test/syscalls/linux:pty_test`
- [     ] `screen` (only non-setuid mode is contemplated)
- [X] `emacs` (with `UPTY_NUM_DEV_TTY=1`)
- [     ] `xterm`
- [     ] `rxvt`
- [     ] `asciinema`
- [     ] Android Terminal
