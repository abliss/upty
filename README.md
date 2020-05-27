# upty - Userspace pseudoterminal

The goal of upty is to provide a functional replacement, purely in userspace,
for the kernel's devpts filesystem.

# Status
This is a hack-in-progress; it doesn't really work yet.

# How to use
To try it out:
1. Fetch the submodules: `git submodule update --init`
2. Start the backend daemon: `go run main.go &`
3. Build the `shim.so`: `make`.
4. Run a simple test with `script` and python: `make test`
5. Run your favorite pty-using binary: `LD_PRELOAD=$PWD/shim.so tmux` (changing 
   CWD not yet supported)

# Design
The idea is to use an `LD_PRELOAD` shim to intercept calls to pty-related glibc
functions (as well as `open` and `ioctl` for non-portable programs which use
those directly). Instead of opening the real `/dev/ptmx` to ask the kernel for a
new "master" pseudoterminal, we connect to a unix domain socket. On the other
end of that socket is a daemon (written in golang) which handles all the line
discipline and terminal settings (using code from gvisor, which is not wired up
yet), and shuttles bytes to/from a second socket which will stand in for the
corresponding "slave" terminal.

# Roadmap
Here are some test programs which use pseudoterminals; perhaps someday they will
work with upty. Right now, an `x` means that I've tried them and gotten basic
functionality, indicating that I've intercepted enough of glibc for them to work
in principle. (Since there's no line discipline implemented yet, nothing works in
practice.)

- [x] `script`
- [x] `tmux`
- [x] `ttyd`
- [01/58] `gvisor` test case `//test/syscalls/linux:pty_test`
- [     ] `screen` (only non-setuid mode is contemplated)
- [     ] `emacs`
- [     ] `xterm`
- [     ] `rxvt`
- [     ] `asciinema`
- [     ] Android Terminal
