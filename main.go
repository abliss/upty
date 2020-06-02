package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"time"
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
	acceptAndRelay("upty");
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
	frontFds[ptsNum] = fd
	return fd, nil
}
type relayer struct {f func()}
func (this relayer) Callback(e *waiter.Entry) {
	this.f()
}
func relayFileToConn(name string, fd *vfs.FileDescription, conn net.Conn) {
	buf := make([]byte, 4096)
	dump := func() {
		n, err := fd.Read(ctx, usermem.BytesIOSequence(buf), vfs.ReadOptions{})
		if err != nil && err != syserror.ErrWouldBlock {
			log.Printf("%s: Error reading %d from fd: %s", name, n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		log.Printf("%s:Read %d from fd: %s",name,n, buf[0:m])
		mybuf := buf
		for n > 0 {
			written, err := conn.Write(mybuf[0:n])
			if err != nil {
				log.Printf("%s:Error writing %d to conn:%s",name, n, err) 
				return;
			}
			n -= int64(written);
			log.Printf("%s:Wrote %d to conn, first %d, %d more",name, written, mybuf[0], n) 
			mybuf = mybuf[written:]
		}
	}
	var e  waiter.Entry
	e.Callback = &relayer{dump}
	fd.EventRegister(&e, waiter.EventIn)
	dump()
}
func relayConnToFile(name string, conn net.Conn, fd *vfs.FileDescription) {
	buf := make([]byte, 4096)
	for {
		conn.SetDeadline(time.Time{})
		n, err := conn.Read(buf)
		if err != nil {
			log.Printf("%s:Error reading %d from conn: %s",name,n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		log.Printf("%s:Read %d from conn: %s",name,n, buf[0:m])
		mybuf := buf
		for n > 0 {
			written, err := fd.Write(ctx, usermem.BytesIOSequence(mybuf[0:n]), 
				vfs.WriteOptions{})
			if err != nil {
				log.Printf("%s:Error writing %d to fd:%s",name, n, err) 
				return;
			}
			n -= int(written);
			log.Printf("%s:Wrote %d to fd, first %d, %d more",name, written, mybuf[0], n) 
			mybuf = mybuf[written:]
		}
	}
}

func acceptAndRelay(name string) {
	os.Remove(name)
	sock, err := net.Listen("unix", name)
	if err != nil {
		log.Fatalf("error on %s Listen(): ", name, err)
	}
	defer sock.Close()
	for {
		log.Printf("Accepting on %s", name) 
		conn, err := sock.Accept()
		if err != nil {
			log.Print("Error on %s Accept():", name, err)
			continue
		}
		log.Printf("Accepted on %s", name)
		buf := make([]byte, 4)
		n, err := conn.Read(buf)
		if err != nil || n < 4 {
			log.Printf("Error on %s: version read(%d):%s", name, n, err)
			continue;
		}
		if (!bytes.Equal(buf, UPTY_VERSION)) {
			log.Printf("Error on %s: version mismatch:%s != %s", 
				name, buf, UPTY_VERSION)
			continue;
		}
		n, err = conn.Read(buf[0:1])
		if err != nil || n < 1 {
			log.Printf("Error on %s: flag read(%d):%s", name, n, err)
			continue;
		}
		if (buf[0] == 0) {
			fd, ptsNum, err := getBackFd()
			if err != nil {
				log.Fatalf("%s:error on fdGet(): %s",name, err)
			}
			binary.LittleEndian.PutUint32(buf, ptsNum)
			n, err = conn.Write(buf)
			if err != nil || n < 4 {
				log.Printf("Error on %s: flag write(%d):%s", name, n, err)
			}
			go relayFileToConn(name, fd, conn)
			go relayConnToFile(name, conn, fd)
		} else if (buf[0] == 1) {
			_, err := conn.Read(buf)
			ptsNum := binary.LittleEndian.Uint32(buf)
			fd, err := getFrontFd(ptsNum)
			if err != nil {
				log.Fatalf("%s:error on getFrontFd(%d): %s",name, ptsNum, err)
			}
			go relayFileToConn(name, fd, conn)
			go relayConnToFile(name, conn, fd)
		} else if (buf[0] == 2) {
			// TODO: pickup
		} else {
			log.Printf("Error on %s: unknown flag %d", name, buf[0])
		}
	}
}


