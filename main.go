package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"runtime/debug"
	"time"
	"syscall"
	"gvisor.dev/gvisor/pkg/abi/linux"
	"gvisor.dev/gvisor/pkg/sentry/arch"
	"gvisor.dev/gvisor/pkg/context"
	"gvisor.dev/gvisor/pkg/fspath"
	"gvisor.dev/gvisor/pkg/sentry/vfs"
	"gvisor.dev/gvisor/pkg/syserror"
	"gvisor.dev/gvisor/pkg/sentry/kernel/auth"
	"gvisor.dev/gvisor/pkg/sentry/fsimpl/devpts"
	"gvisor.dev/gvisor/pkg/usermem"
	"gvisor.dev/gvisor/pkg/waiter"
)

var (
	//path = flag.String("path", "", "Path to directory to serve")
	//addr = flag.String("socket", "", "Path to unix socket to listen on")
	UPTY_VERSION = []byte("upty");
	ctx = context.Background()  // TODO: will fail if task values are needed, e.g. setControllingTTY
	backFds = make([]*vfs.FileDescription, 256);
	frontFds = make([]*vfs.FileDescription, 256);
	opener func(string) (*vfs.FileDescription, error)
)

func main() {
	flag.Parse()
	log.Printf("Name: %s", devpts.Name)
	var vfsObj vfs.VirtualFilesystem
	vfsObj.Init()
	creds := auth.NewAnonymousCredentials()
	source := "source"
	var opts vfs.GetFilesystemOptions
	var fstype devpts.FilesystemType

	devptsObj, devptsRoot, err := fstype.GetFilesystem(
		ctx,
		&vfsObj,
		creds,
		source,
		opts)

	mnt, err := vfsObj.NewDisconnectedMount(devptsObj, devptsRoot, 
		&vfs.MountOptions{})
	if err != nil {
		log.Fatalf("error on mount create", err)
	}
	
	vdir := vfs.MakeVirtualDentry(mnt, devptsRoot)
	var ordwr vfs.OpenOptions
	ordwr.Flags = linux.O_RDWR
	opener = func(path string) (*vfs.FileDescription, error) {
		return vfsObj.OpenAt(ctx, auth.CredentialsFromContext(ctx),
			&vfs.PathOperation{vdir, vdir, fspath.Parse(path), true}, 
			&ordwr)
	}
	acceptAndRelay(".upty-sock");
}

func getBackFd() (*vfs.FileDescription, uint32, error) {
	fd, err := opener("ptmx")
	if (err != nil) {
		return fd, 0, err
	}
	buf := make([]byte, 4)
	_, err = fd.Impl().Ioctl(ctx, &usermem.BytesIO{buf},
		arch.SyscallArguments{
			arch.SyscallArgument{},
			arch.SyscallArgument{Value: uintptr(linux.TIOCGPTN)}});
	if (err != nil) {
		return fd, 0, err
	}
	ptsNum := binary.LittleEndian.Uint32(buf)
	log.Printf("Got ptsNum %d", ptsNum)
	if (ptsNum >= uint32(len(backFds))) {
		log.Fatalf("too many backfds");
	}
	log.Printf("upty: Setting backFds[%d]=%s", ptsNum, fd)
	backFds[ptsNum] = fd
	return fd, ptsNum, nil
}
func getFrontFd(ptsNum uint32) (*vfs.FileDescription, error) {
 	fd, err := opener(fmt.Sprintf("%d",ptsNum))
	if (err != nil) {
		return fd, err
	}
	if (ptsNum >= uint32(len(frontFds))) {
		log.Fatalf("too many frontfds");
	}
	log.Printf("upty: Setting frontFds[%d]=%s", ptsNum, fd)
	frontFds[ptsNum] = fd
	return fd, nil
}
type callback struct {cb func()}
func (this callback) Callback(e *waiter.Entry) {
	this.cb()
}
func newWaiterEntry(cb func()) waiter.Entry {
	var e waiter.Entry
	e.Callback = &callback{cb}
	return e
}
func relayFileToConn(fd *vfs.FileDescription, conn net.Conn) {
	buf := make([]byte, 4096)
	wakeUp := make(chan int, 4096)
	e := newWaiterEntry(func(){wakeUp<-0})
	fd.EventRegister(&e, waiter.EventIn)
	f := newWaiterEntry(func(){close(wakeUp)})
	fd.EventRegister(&f, waiter.EventHUp)
	for _ = range wakeUp {
		n, err := fd.Read(ctx, usermem.BytesIOSequence(buf), vfs.ReadOptions{})
		if err != nil && err != syserror.ErrWouldBlock {
			log.Printf("upty: Error reading %d from fd: %s", n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		//log.Printf("upty:Read %d from fd: %s",n, buf[0:m])
		defer RecoverAnd(func() {
			fd.EventUnregister(&e)
		})
		fully(conn.Write, buf[0:n])
	}
}
func relayConnToFile(conn net.Conn, fd *vfs.FileDescription) {
	buf := make([]byte, 4096)
	defer Recover();
	defer conn.Close()
	defer fd.DecRef()

	for {
		conn.SetDeadline(time.Time{})
		n, err := conn.Read(buf)
		if err != nil {
			log.Printf("upty:Error reading %d from conn: %s",n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		//log.Printf("upty:Read %d from conn: %s",n, buf[0:m])
		fully(func (buf []byte) (int, error) {
			n64, err2 := fd.Write(ctx, usermem.BytesIOSequence(buf), 
				vfs.WriteOptions{})
			return int(n64), err2
		}, buf[0:n])
	}
}
func Recover() {
	if err := recover(); err != nil {
		log.Printf("upty:Recovering error: %s\n%s\n", err, string(debug.Stack()))
	}
}
func RecoverAnd(f func()) {
	if err := recover(); err != nil {
		log.Printf("upty:Recovering error: %s\n%s\n", err, string(debug.Stack()))
		f()
	}
}

func fully(rw func ([]byte) (int, error), buf []byte) {
	for len(buf) > 0 {
		n, err := rw(buf);
		if n <= 0 || err != nil {
			log.Printf("Error in fully: n=%d, err=%s, rw=%s", n, err, rw)
			panic(err)
		}
		buf = buf[n:]
	}
}
func handleIoctl(conn net.Conn) {
	defer conn.Close()
	defer Recover()
	hBuf := make([]byte, 8)
	fully(conn.Read, hBuf[0:4])
	ptsNum := binary.LittleEndian.Uint32(hBuf[0:4])
	fully(conn.Read, hBuf[0:1])
	isBack := hBuf[0] != 0
	fully(conn.Read, hBuf[0:8])
	request := binary.LittleEndian.Uint64(hBuf[0:8])
	fully(conn.Read, hBuf[0:1])
	argT := int(hBuf[0])
	fully(conn.Read, hBuf[0:1])
	nBytes := int(hBuf[0])
	aBuf := make([]byte, nBytes)
	fully(conn.Read, aBuf)
	log.Printf("upty:ioctl on num=%d, back=%s, req=%x, argT=%d, bytes=%d",
		ptsNum, isBack, request, argT, nBytes)
	var fd *vfs.FileDescription
	if (isBack) {
		fd = backFds[ptsNum]
	} else {
		fd = frontFds[ptsNum]
	}
	if fd == nil {
		panic("fd not ok! ")
	}
	var syscallArgs arch.SyscallArguments
	syscallArgs[0] = arch.SyscallArgument{uintptr(16)} // ioctl
	syscallArgs[1] = arch.SyscallArgument{uintptr(request)}
	if (argT == 0) {
		// special case, arg is literal int, not pointer
		syscallArgs[2] = arch.SyscallArgument{uintptr(binary.LittleEndian.Uint64(aBuf))}
	} else {
		// for all others, the arg is a pointer to the beginning of buf
		syscallArgs[2] = arch.SyscallArgument{uintptr(0)}
	}

	var ret uintptr;
	var err error;
	switch(request) {
	case linux.TIOCSCTTY:
	case linux.TIOCGPGRP:
	case linux.TIOCNOTTY:
	case linux.TIOCSPGRP:
		log.Printf("TODO: cannot handle process ioctls yet!")
	default:
		ret, err = fd.Impl().Ioctl(ctx, &usermem.BytesIO{aBuf}, syscallArgs)
	}
	var errno uint32
	if err != nil {
		errno = uint32(err.(syscall.Errno))
	}
	fully(conn.Write, aBuf);
	// return code
	buf := make([]byte, 4)
	binary.LittleEndian.PutUint32(buf, uint32(ret))
	fully(conn.Write, buf);
	// errno
	binary.LittleEndian.PutUint32(buf, errno)
	fully(conn.Write, buf);
	log.Printf("upty: ioctl done on num=%d, back=%s, req=%x, argT=%d, bytes=%d" +
		" ret=%d err=%d",
		ptsNum, isBack, request, argT, nBytes, ret, errno)
}
func acceptAndRelay(socketPath string) {
	os.Remove(socketPath)
	sock, err := net.Listen("unix", socketPath)
	if err != nil {
		log.Fatalf("error on %s Listen(): ", socketPath, err)
	}
	for {
		log.Printf("Accepting on %s", socketPath) 
		conn, err := sock.Accept()
		if err != nil {
			log.Print("Error on %s Accept():", socketPath, err)
			continue
		}
		defer Recover();

		log.Printf("Accepted on %s", socketPath)
		buf := make([]byte, 4)
		fully(conn.Read, buf);
		if (!bytes.Equal(buf, UPTY_VERSION)) {
			log.Printf("upty:Error: version mismatch:%s != %s", 
				buf, UPTY_VERSION)
			conn.Close()
			continue;
		}
		fully(conn.Read,buf[0:1])
		switch(buf[0]) {
		case 0:
			fd, ptsNum, err := getBackFd()
			if err != nil {
				log.Fatalf("upty:error on fdGet(): %s", err)
			}
			binary.LittleEndian.PutUint32(buf, ptsNum)
			fully(conn.Write,buf)
			go relayFileToConn(fd, conn)
			go relayConnToFile(conn, fd)
		case 1:
			fully(conn.Read, buf)
			ptsNum := binary.LittleEndian.Uint32(buf)
			fd, err := getFrontFd(ptsNum)
			if err != nil {
				log.Fatalf("upty:error on getFrontFd(%d): %s", ptsNum, err)
			}
			log.Printf("Got front for ptsNum %d: %d", ptsNum, fd)
			go relayFileToConn(fd, conn)
			go relayConnToFile(conn, fd)
		case 2:
			go handleIoctl(conn)
		default:
			log.Printf("upty:Error on %s: unknown flag %d", buf[0])
			conn.Close()
			}
	}
}


