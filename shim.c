#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define upty_debug(...) if (getenv("UPTY_DEBUG")) {fprintf(stderr, __VA_ARGS__);}

static const char* env_var_format = "__UPTY_NUM_%d";
static const char* UPTY_VERSION = "upty";
static const char UPTY_BACK = 0;
static const char UPTY_FRONT = 1;
static const char UPTY_IOCTL = 2;
static char UPTY_SOCKET[256];

enum ioctl_arg_t {
  int_,
  int_p,
  winsize_p,
  termio_p,
  termios_p,
  char_p,
  void_,
  unknown_,
};
char* get_socket_path() {
  if (UPTY_SOCKET[0] == 0) {
    char* env = getenv("UPTY_SOCKET");
    if (env) {
      strncpy(UPTY_SOCKET, env, sizeof(UPTY_SOCKET));
    } else if (getenv("HOME")) {
      snprintf(UPTY_SOCKET, sizeof(UPTY_SOCKET), 
               "%s/.upty-sock", getenv("HOME"));
    } else {
      snprintf(UPTY_SOCKET, sizeof(UPTY_SOCKET),
               "/run/upty-%d/sock", getuid());
    }
  }
  upty_debug("upty: Socket path: %s\n", UPTY_SOCKET);
  return UPTY_SOCKET;
}
int set_upty_num(int fd, int upty_num) {
  char key[30];
  snprintf(key, sizeof(key), env_var_format, fd);
  if (upty_num < 0) {
    upty_debug("upty: unsetting %s\n", key);
    return unsetenv(key);
  } else {
    char val[12];
    snprintf(val, sizeof(val), "%d", upty_num);
    upty_debug("upty: setting %s=%s\n", key, val);
    int ret = setenv(key, val, 1);
    if (ret < 0) {
      perror("upty:error in setenv");
    }
    return ret;
  }
}

int get_upty_num(int fd) {
  char key[30];
  snprintf(key, sizeof(key), env_var_format, fd);
  char* val = getenv(key);
  if (val) {
    return atoi(val);
  } else {
    return -1;
  }
}
int getpt() {
  upty_debug("upty: Intercepted getpt\n");
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, get_socket_path(), sizeof(addr.sun_path)-1);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("upty: error connecting to back");
    exit(-1);
  }
  write(fd, UPTY_VERSION, 4);
  write(fd, &UPTY_BACK, 1);
  int pty_num;
  int r = read(fd, &pty_num, 4);
  if (r != 4) {
    perror("upty: Bad read on connect");
    return -1;
  }
  set_upty_num(fd, pty_num * 2);
  upty_debug("upty: Returning fake back %d=%d\n", fd, pty_num);
  return fd;
}

int get_front_fd(int back_upty_num) {
  int front_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, get_socket_path(), sizeof(addr.sun_path)-1);
  if (connect(front_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("upty: error connecting to front");
    return -1;
  }
  write(front_fd, UPTY_VERSION, 4);
  write(front_fd, &UPTY_FRONT, 1);
  write(front_fd, &back_upty_num, 4);
  set_upty_num(front_fd, back_upty_num * 2 + 1);
  upty_debug("upty: Returning fake front %d\n", front_fd);
  return front_fd;
}

typedef int (*ioctl1_t)(int, unsigned long int, unsigned long int);

int ioctl1(int fd, unsigned long int request, unsigned long int  data) {
  return ((ioctl1_t)dlsym(RTLD_NEXT, "ioctl"))(fd, request, data);
}

//int ioctl(int fd, unsigned long request, void* arg1) {
int ioctl(int fd, unsigned long int  request, ...) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted ioctl: %d==%d %lx\n", 
            fd, upty_num, request);
  va_list ap;
  va_start(ap, request);
  unsigned long int arg1 = va_arg (ap, unsigned long int);
  va_end(ap);
  if (upty_num < 0) {
    return ioctl1(fd, request, arg1);
  }
  enum ioctl_arg_t arg_t = unknown_;
  switch(request) {
  case TIOCGPTPEER: {
    return get_front_fd(upty_num / 2);
  }
  case TIOCGPTN: {
    // see ptsname.c
    // arg1 is a pointer to an unsigned int pty number.
    int* outptr = (int*) arg1;
    int num = get_upty_num(fd) / 2;
    upty_debug("upty: TIOCGPTN: Returning upty num %d\n", num);
    *outptr = num;
    return 0;
  }
  // The following ioctls are grouped by argument type from ioctl_tty(2):
  case TIOCGWINSZ:
  case TIOCSWINSZ: {
    // TODO see openpty.c
    arg_t = winsize_p; // struct winsize*
    break;
  }
  case TCSETS:
  case TCSETSW:
  case TCSETSF:
  case TCGETS:
  case TIOCGLCKTRMIOS:
  case TIOCSLCKTRMIOS:
    {
      // TODO: see tcsetattr.c / tcgetattr.c
      arg_t = termios_p; // struct termios*
      break;
    }
  case TCGETA:
  case TCSETA:
  case TCSETAW:
  case TCSETAF:
    {
      arg_t = termio_p;    // struct termio*
      break;
    }
  case TCSBRK:
  case TCSBRKP:
  case TCXONC:
  case TCFLSH:
  case TIOCSCTTY: // TODO: see login_tty.c -- become controlling tty
    {
      arg_t = int_; // int
      break;
    }
  case TIOCSBRK:
  case TIOCCBRK:
  case TIOCCONS:
  case TIOCNOTTY:
  case TIOCEXCL:
  case TIOCNXCL:
    {
      arg_t = void_;// void
      break;
    }
  case FIONREAD: // aka TIOCINQ
  case TIOCOUTQ:
  case TIOCSETD:
  case TIOCGETD:
  case TIOCGPGRP: // actually pid_t* but TODO assume that's int
  case TIOCSPGRP:
  case TIOCGSID:
  case TIOCPKT:
  case TIOCGPKT:
  case TIOCSPTLCK:
    {
      arg_t = int_p; // int*
      break;
    }
  case TIOCSTI:
    {
      arg_t = char_p; // char *argp
      break;
    }
  case TIOCMGET:
  case TIOCMBIS:
  case TIOCMBIC:
  case TIOCMSET:
  case TIOCGSOFTCAR:
  case TIOCSSOFTCAR:
  case TIOCLINUX:
  case TIOCGSERIAL:
  case TIOCSSERIAL:
  case FIONBIO:
    /*
      case TCGETS2:
      case TCSETS2:
      case TCSETSW2:
      case TCSETSF2:
    */
  case TIOCGRS485:
  case TIOCGDEV:
  case TCGETX:
  case TCSETX:
  case TCSETXF:
  case TCSETXW:
  case TIOCSIG:
  case TIOCVHANGUP:
  case TIOCGPTLCK:
  case TIOCGEXCL:
  case FIONCLEX:
  case FIOCLEX:
  case FIOASYNC:
  case TIOCSERCONFIG:
  case TIOCSERGWILD:
  case TIOCSERSWILD:
  case TIOCSERGSTRUCT:
  case TIOCSERGETLSR:
  case TIOCSERGETMULTI:
  case TIOCSERSETMULTI:
  case TIOCMIWAIT:
  case TIOCGICOUNT:
  case TIOCPKT_DATA:
  case TIOCPKT_FLUSHREAD: // aka TIOCSER_TEMT
  case TIOCPKT_FLUSHWRITE:
  case TIOCPKT_STOP:
  case TIOCPKT_START:
  case TIOCPKT_NOSTOP:
  case TIOCPKT_DOSTOP:
  case TIOCPKT_IOCTL: {
    upty_debug("upty: Ioctl not implemented : %lu\n", arg1);
    return 0;
  }
  default: {
    upty_debug("upty: Unknown ioctl : %lu\n", arg1);
    return 0;
  }
  }
  int ioctl_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, get_socket_path(), sizeof(addr.sun_path)-1);
  if (connect(ioctl_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("upty: error connecting to ioctl");
    return -1;
  }
  write(ioctl_fd, UPTY_VERSION, 4);
  write(ioctl_fd, &UPTY_IOCTL, 1);

  int basenum = upty_num / 2;
  write(ioctl_fd, &basenum, 4);
  char is_master = upty_num % 2 ? 1 : 0; 
  write(ioctl_fd, &is_master, 1);
  write(ioctl_fd, &request, sizeof(request));
  char arg_t_c = (char)arg_t;
  write(ioctl_fd, &arg_t_c, 1);
  char nbytes;
  int n = 0;
  switch(arg_t) {
  case void_: {
    nbytes = 0;
    write(ioctl_fd, &nbytes, 1);
    break;
  }
  case int_: {
    nbytes = sizeof(arg1);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg1, sizeof(arg1));
    break;
  }
  case int_p: {
    int arg = *((int*)arg1);
    nbytes = sizeof(arg);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg, sizeof(arg));
    n = read(ioctl_fd, &arg, sizeof(arg));
    upty_debug("upty: Read for ioctl : %d\n", n);
    *((int*)arg1) = arg;
    break;
  }
  case char_p: {
    char arg = *((char*)arg1);
    nbytes = sizeof(arg);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg, sizeof(arg));
    n = read(ioctl_fd, &arg, sizeof(arg));
    upty_debug("upty: Read for ioctl : %d\n", n);

    *((char*)arg1) = arg;
    break;
  }
  case winsize_p: {
    struct winsize arg = *((struct winsize*)arg1);
    nbytes = sizeof(arg);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg, sizeof(arg));
    n = read(ioctl_fd, &arg, sizeof(arg));
    upty_debug("upty: Read for ioctl : %d\n", n);

    *((struct winsize*)arg1) = arg;
    break;
  }
  case termio_p: {
    struct termio arg = *((struct termio*)arg1);
    nbytes = sizeof(arg);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg, sizeof(arg));
    n = read(ioctl_fd, &arg, sizeof(arg));
    upty_debug("upty: Read for ioctl : %d\n", n);
    *((struct termio*)arg1) = arg;
    break;
  }
  case termios_p: {
    struct termios arg = *((struct termios*)arg1);
    nbytes = sizeof(arg);
    write(ioctl_fd, &nbytes, 1);
    write(ioctl_fd, &arg, sizeof(arg));
    n = read(ioctl_fd, &arg, sizeof(arg));
    upty_debug("upty: Read for ioctl : %d\n", n);
    *((struct termios*)arg1) = arg;
    break;
  }
  case unknown_: {
    upty_debug("upty: Ioctl not implemented : %lu\n", arg1);
    return -1;
  }
  }
  int ret_code, err_no;
  n = read(ioctl_fd, &ret_code, sizeof(ret_code));
  upty_debug("upty: Read for ioctl : %d\n", n);
  n = read(ioctl_fd, &err_no, sizeof(err_no));
  upty_debug("upty: Read for ioctl : %d\n", n);
  close(ioctl_fd);
  if (ret_code < 0) {
    errno = err_no;
  }
  upty_debug("upty: Returning from ioctl : %d\n", ret_code);
  return ret_code;
}


typedef int (*isatty_t)(int);
int isatty(int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted isatty(%d)==%d\n", fd, upty_num);
  if (upty_num < 0) {
    return ((isatty_t)dlsym(RTLD_NEXT, "isatty"))(fd);
  }
  return 1;
}
int __isatty(int fd) {
  return isatty(fd);
}

typedef char* (*ptsname_t)(int);
char* ptsname (int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted ptsname(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return "fake_upty";
  } else {
    return ((ptsname_t)dlsym(RTLD_NEXT, "ptsname"))(fd);
  }
}

/* Store at most BUFLEN characters of the pathname of the slave pseudo
   terminal associated with the master FD is open on in BUF.
   Return 0 on success, otherwise an error number.  */
typedef int (*ptsname_r_t)(int, char*, size_t);

int ptsname_r (int fd, char *buf, size_t buflen) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted ptsname_r(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    strncpy(buf, "fake_upty", buflen);
    return 0;
  } else {
    return ((ptsname_r_t)dlsym(RTLD_NEXT, "ptsname_r"))(fd, buf, buflen);
  }
}

typedef int (*grantpt_t)(int);
int grantpt (int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted grantpt(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return 0;
  } else {
    return ((grantpt_t)dlsym(RTLD_NEXT, "grantpt"))(fd);
  }
}

typedef int (*unlockpt_t)(int);
int unlockpt (int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted unlockpt(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return 0;
  } else {
    return ((unlockpt_t)dlsym(RTLD_NEXT, "unlockpt"))(fd);
  }
}

typedef char* (*ttyname_t)(int);
char* ttyname (int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted ttyname(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return "fake_upty";
  } else {
    return ((ttyname_t)dlsym(RTLD_NEXT, "ttyname"))(fd);
  }
}


typedef int (*dup_t)(int);
int dup(int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted dup(%d)==%d\n", fd, upty_num);
  int dup_fd = ((dup_t)dlsym(RTLD_NEXT, "dup"))(fd);
  if (upty_num >= 0) {
    if (dup_fd >= 0) {
      set_upty_num(dup_fd, upty_num);
    }
  }
  return dup_fd;
}
typedef int (*dup2_t)(int, int);
int dup2(int fd, int newfd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted dup2(%d)==%d\n", fd, upty_num);
  int dup_fd = ((dup2_t)dlsym(RTLD_NEXT, "dup2"))(fd, newfd);
  if (upty_num >= 0) {
    if (dup_fd >= 0) {
      set_upty_num(dup_fd, upty_num);
    }
  }
  return dup_fd;
}
typedef int (*dup3_t)(int, int, int);
int dup3(int fd, int newfd, int flags) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted dup3(%d)==%d\n", fd, upty_num);
  int dup_fd = ((dup3_t)dlsym(RTLD_NEXT, "dup3"))(fd, newfd, flags);
  if (upty_num >= 0) {
    if (dup_fd >= 0) {
      set_upty_num(dup_fd, upty_num);
    }
  }
  return dup_fd;
  
}

typedef int (*open_t)(const char*, int);
typedef int (*open3_t)(const char*, int, mode_t);
int open(const char *pathname, int flags, ...) {
  upty_debug("upty: Intercepted open(%s,%x)\n", pathname, flags);
  if (strcmp("/dev/ptmx", pathname) == 0) {
    return getpt();
  } else if (strncmp("/dev/pts/", pathname, 9) == 0) {
    int pty_num = atoi(pathname + 9);
    if (pty_num <= 0) {
      errno = ENOENT;
      return -1;
    }
    return get_front_fd(pty_num);
  }
  va_list ap;
  va_start(ap, flags);
  int ret;
  if (flags) {
    mode_t mode = va_arg (ap, mode_t);
    ret = ((open3_t)dlsym(RTLD_NEXT, "open"))(pathname, flags, mode);
  } else {
    ret = ((open_t)dlsym(RTLD_NEXT, "open"))(pathname, flags);
  }
  va_end(ap);
  return ret;
}


/* TODO
int openat(int dirfd, const char *pathname, int flags);
int openat(int dirfd, const char *pathname, int flags, mode_t mode);
*/
typedef int (*close_t)(int);
int close(int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted close(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    set_upty_num(fd, -1);
  }
  return ((close_t)dlsym(RTLD_NEXT, "close"))(fd);
}

typedef pid_t (*tcgetpgrp_t)(int);
pid_t tcgetpgrp (int fd) {
  int upty_num = get_upty_num(fd);
  upty_debug("upty: Intercepted tcgetpgrp(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    //return (pid_t)5324; //XXXXX
  }
  return ((tcgetpgrp_t)dlsym(RTLD_NEXT, "tcgetpgrp"))(fd);
}
