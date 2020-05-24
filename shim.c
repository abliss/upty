#define _GNU_SOURCE

#include <dlfcn.h>
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

/*
int fcntl(int fd, int cmd, ... ) {
 
}

int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp,
            const struct winsize *winp) {
}
*/
static const char* env_var_format = "__PYTD_NUM_%d";
int set_ptyd_num(int fd, int ptyd_num) {
  char key[30];
  char val[12];
  snprintf(key, sizeof(key), env_var_format, fd);
  snprintf(val, sizeof(val), "%d", ptyd_num);
  int ret = setenv(key, val, 1);
  if (ret < 0) {
    perror("error in setenv");
  }
  return ret;
}

int get_ptyd_num(int fd) {
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
  set_ptyd_num(fd, 1);
  pty_debug("XXXX Returning fake back %d\n", fd);
  return fd;
}

typedef int (*ioctl1_t)(int, unsigned long int, unsigned long int);

int ioctl1(int fd, unsigned long int request, unsigned long int  data) {
  return ((ioctl1_t)dlsym(RTLD_NEXT, "ioctl"))(fd, request, data);
}

//int ioctl(int fd, unsigned long request, void* arg1) {
int ioctl(int fd, unsigned long int  request, ...) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted ioctl: %d==%d %lu\n", 
            fd, ptyd_num, request);
  va_list ap;
  va_start(ap, request);
  unsigned long int arg1 = va_arg (ap, unsigned long int);
  va_end(ap);
  if (ptyd_num < 0) {
    return ioctl1(fd, request, arg1);
  }
  switch(request) {
  case TIOCGPTPEER: {
    int front_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "front", sizeof(addr.sun_path)-1);
    if (connect(front_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
      perror("error connecting to front");
      exit(-1);
    }
    set_ptyd_num(front_fd, 2);
    pty_debug("XXXX Returning fake front %d\n", front_fd);
    return front_fd;
  }
  case TIOCGPTN: {
    // see ptsname.c
    // arg1 is a pointer to an unsigned int pty number.
    int* outptr = (int*) arg1;
    int num = get_ptyd_num(fd);
    pty_debug("XXXX TIOCGPTN: Returning ptyd num %d\n", num);
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
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted isatty(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num < 0) {
    return ((isatty_t)dlsym(RTLD_NEXT, "isatty"))(fd);
  }
  return 1;
}
int __isatty(int fd) {
  return isatty(fd);
}

typedef char* (*ptsname_t)(int);
char* ptsname (int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted ptsname(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    return "fake_ptyd";
  } else {
    return ((ptsname_t)dlsym(RTLD_NEXT, "ptsname"))(fd);
  }
}

/* Store at most BUFLEN characters of the pathname of the slave pseudo
   terminal associated with the master FD is open on in BUF.
   Return 0 on success, otherwise an error number.  */
typedef int (*ptsname_r_t)(int, char*, size_t);

int ptsname_r (int fd, char *buf, size_t buflen) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted ptsname_r(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    strncpy(buf, "fake_ptyd", buflen);
    return 0;
  } else {
    return ((ptsname_r_t)dlsym(RTLD_NEXT, "ptsname_r"))(fd, buf, buflen);
  }
}

typedef int (*grantpt_t)(int);
int grantpt (int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted grantpt(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    return 0;
  } else {
    return ((grantpt_t)dlsym(RTLD_NEXT, "grantpt"))(fd);
  }
}

typedef int (*unlockpt_t)(int);
int unlockpt (int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted unlockpt(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    return 0;
  } else {
    return ((unlockpt_t)dlsym(RTLD_NEXT, "unlockpt"))(fd);
  }
}

typedef char* (*ttyname_t)(int);
char* ttyname (int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted ttyname(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    return "fake_ptyd";
  } else {
    return ((ttyname_t)dlsym(RTLD_NEXT, "ttyname"))(fd);
  }
}


typedef int (*dup_t)(int);
int dup(int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted dup(%d)==%d\n", fd, ptyd_num);
  int dup_fd = ((dup_t)dlsym(RTLD_NEXT, "dup"))(fd);
  if (ptyd_num >= 0) {
    if (dup_fd >= 0) {
      set_ptyd_num(dup_fd, ptyd_num);
    }
  }
  return dup_fd;
}
typedef int (*dup2_t)(int, int);
int dup2(int fd, int newfd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted dup2(%d)==%d\n", fd, ptyd_num);
  int dup_fd = ((dup2_t)dlsym(RTLD_NEXT, "dup2"))(fd, newfd);
  if (ptyd_num >= 0) {
    if (dup_fd >= 0) {
      set_ptyd_num(dup_fd, ptyd_num);
    }
  }
  return dup_fd;
}
typedef int (*dup3_t)(int, int, int);
int dup3(int fd, int newfd, int flags) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted dup3(%d)==%d\n", fd, ptyd_num);
  int dup_fd = ((dup3_t)dlsym(RTLD_NEXT, "dup3"))(fd, newfd, flags);
  if (ptyd_num >= 0) {
    if (dup_fd >= 0) {
      set_ptyd_num(dup_fd, ptyd_num);
    }
  }
  return dup_fd;
  
}

typedef int (*close_t)(int);
int close(int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted close(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    set_ptyd_num(fd, -1);
  }
  return ((close_t)dlsym(RTLD_NEXT, "close"))(fd);
}

typedef pid_t (*tcgetpgrp_t)(int);
pid_t tcgetpgrp (int fd) {
  int ptyd_num = get_ptyd_num(fd);
  pty_debug("XXXX Intercepted tcgetpgrp(%d)==%d\n", fd, ptyd_num);
  if (ptyd_num >= 0) {
    return (pid_t)5324; //XXXXX
  }
  return ((tcgetpgrp_t)dlsym(RTLD_NEXT, "tcgetpgrp"))(fd);
}
