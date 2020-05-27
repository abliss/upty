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

#define pty_debug(...) fprintf(stderr, __VA_ARGS__)

static const char* env_var_format = "__UPTY_NUM_%d";
int set_upty_num(int fd, int upty_num) {
  char key[30];
  char val[12];
  snprintf(key, sizeof(key), env_var_format, fd);
  snprintf(val, sizeof(val), "%d", upty_num);
  int ret = setenv(key, val, 1);
  if (ret < 0) {
    perror("error in setenv");
  }
  return ret;
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
  pty_debug("XXXX Intercepted getpt\n");
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "back", sizeof(addr.sun_path)-1);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("error connecting to back");
    exit(-1);
  }
  set_upty_num(fd, 1);
  pty_debug("XXXX Returning fake back %d\n", fd);
  return fd;
}

int get_front_fd(int back_upty_num) {
  int front_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "front", sizeof(addr.sun_path)-1);
  if (connect(front_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("error connecting to front");
    return -1;
  }
  set_upty_num(front_fd, 2);
  pty_debug("XXXX Returning fake front %d\n", front_fd);
  return front_fd;
}

typedef int (*ioctl1_t)(int, unsigned long int, unsigned long int);

int ioctl1(int fd, unsigned long int request, unsigned long int  data) {
  return ((ioctl1_t)dlsym(RTLD_NEXT, "ioctl"))(fd, request, data);
}

//int ioctl(int fd, unsigned long request, void* arg1) {
int ioctl(int fd, unsigned long int  request, ...) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted ioctl: %d==%d %lu\n", 
            fd, upty_num, request);
  va_list ap;
  va_start(ap, request);
  unsigned long int arg1 = va_arg (ap, unsigned long int);
  va_end(ap);
  if (upty_num < 0) {
    return ioctl1(fd, request, arg1);
  }
  switch(request) {
  case TIOCGPTPEER: {
    return get_front_fd(upty_num);
  }
  case TIOCGPTN: {
    // see ptsname.c
    // arg1 is a pointer to an unsigned int pty number.
    int* outptr = (int*) arg1;
    int num = get_upty_num(fd);
    pty_debug("XXXX TIOCGPTN: Returning upty num %d\n", num);
    *outptr = num;
    return 0;
  }
  case TIOCSCTTY: {
    // TOOD: see login_tty.c
    // become controlling tty
    return 0;
  }

  case TIOCGWINSZ:
  case TIOCSWINSZ:
    // TODO see openpty.c
  case TCSETS:
  case TCSETSW:
  case TCSETSF:
    // TODO: see tcsetattr.c

  case TCGETS:
    // TODO: see tcgetattr.c
  case TCGETA:
  case TCSETA:
  case TCSETAW:
  case TCSETAF:
  case TCSBRK:
  case TCXONC:
  case TCFLSH:
  case TIOCEXCL:
  case TIOCNXCL:
  case TIOCGPGRP:
  case TIOCSPGRP:
  case TIOCOUTQ:
  case TIOCSTI:
  case TIOCMGET:
  case TIOCMBIS:
  case TIOCMBIC:
  case TIOCMSET:
  case TIOCGSOFTCAR:
  case TIOCSSOFTCAR:
  case FIONREAD: // aka TIOCINQ
  case TIOCLINUX:
  case TIOCCONS:
  case TIOCGSERIAL:
  case TIOCSSERIAL:
  case TIOCPKT:
  case FIONBIO:
  case TIOCNOTTY:
  case TIOCSETD:
  case TIOCGETD:
  case TCSBRKP:
  case TIOCSBRK:
  case TIOCCBRK:
  case TIOCGSID:
    /*
      case TCGETS2:
      case TCSETS2:
      case TCSETSW2:
      case TCSETSF2:
    */
  case TIOCGRS485:
  case TIOCSPTLCK:
  case TIOCGDEV:
  case TCGETX:
  case TCSETX:
  case TCSETXF:
  case TCSETXW:
  case TIOCSIG:
  case TIOCVHANGUP:
  case TIOCGPKT:
  case TIOCGPTLCK:
  case TIOCGEXCL:
  case FIONCLEX:
  case FIOCLEX:
  case FIOASYNC:
  case TIOCSERCONFIG:
  case TIOCSERGWILD:
  case TIOCSERSWILD:
  case TIOCGLCKTRMIOS:
  case TIOCSLCKTRMIOS:
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
  case TIOCPKT_IOCTL:
    pty_debug("XXXX Ioctl not implemented : %lu\n", arg1);
    return 0;
  default:
    pty_debug("XXXX Unknown ioctl : %lu\n", arg1);
    return 0;}
  
}


typedef int (*isatty_t)(int);
int isatty(int fd) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted isatty(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted ptsname(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted ptsname_r(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted grantpt(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return 0;
  } else {
    return ((grantpt_t)dlsym(RTLD_NEXT, "grantpt"))(fd);
  }
}

typedef int (*unlockpt_t)(int);
int unlockpt (int fd) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted unlockpt(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return 0;
  } else {
    return ((unlockpt_t)dlsym(RTLD_NEXT, "unlockpt"))(fd);
  }
}

typedef char* (*ttyname_t)(int);
char* ttyname (int fd) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted ttyname(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return "fake_upty";
  } else {
    return ((ttyname_t)dlsym(RTLD_NEXT, "ttyname"))(fd);
  }
}


typedef int (*dup_t)(int);
int dup(int fd) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted dup(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted dup2(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted dup3(%d)==%d\n", fd, upty_num);
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
  pty_debug("XXXX Intercepted open(%s,%x)\n", pathname, flags);
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
  pty_debug("XXXX Intercepted close(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    set_upty_num(fd, -1);
  }
  return ((close_t)dlsym(RTLD_NEXT, "close"))(fd);
}

typedef pid_t (*tcgetpgrp_t)(int);
pid_t tcgetpgrp (int fd) {
  int upty_num = get_upty_num(fd);
  pty_debug("XXXX Intercepted tcgetpgrp(%d)==%d\n", fd, upty_num);
  if (upty_num >= 0) {
    return (pid_t)5324; //XXXXX
  }
  return ((tcgetpgrp_t)dlsym(RTLD_NEXT, "tcgetpgrp"))(fd);
}
