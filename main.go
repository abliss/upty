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
	ctx = context.Background()  // TOD: will fail if task values are needed, e.g. setControllingTTY
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
	backPath := vfs.PathOperation{vdir, vdir, fspath.Parse("ptmx"), true}
	var ordwr vfs.OpenOptions
	ordwr.Flags = linux.O_RDWR
	doRelay("back", func () (*vfs.FileDescription, error) {
		fd, err := vfsObj.OpenAt(ctx, auth.CredentialsFromContext(ctx),
			&backPath, &ordwr)
		if (err != nil) {
			return fd, err
		}
		buf := make([]byte, 4)
		_, err = fd.Impl().Ioctl(ctx, &usermem.BytesIO{buf},
			arch.SyscallArguments{
				arch.SyscallArgument{},
				arch.SyscallArgument{Value: uintptr(linux.TIOCGPTN)}});
		if (err != nil) {
			return fd, err
		}
		ptsNum := binary.LittleEndian.Uint32(buf)
		log.Printf("Got ptsNum %d", ptsNum)
		frontPath := vfs.PathOperation{vdir, vdir,
			fspath.Parse(fmt.Sprintf("%d",ptsNum)), true}

		go doRelay("front", func() (*vfs.FileDescription, error) {
			frontFd, err := vfsObj.OpenAt(ctx, auth.CredentialsFromContext(ctx),
				&frontPath,
				&ordwr)
			return frontFd, err
		})
		return fd, nil
	});
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

func doRelay(name string, fdGet func() (*vfs.FileDescription, error)) {
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
		fd, err := fdGet()
		if err != nil {
			log.Fatalf("%s:error on fdGet(): %s",name, err)
		}
		
		//errch := make(chan error)
		go relayFileToConn(name, fd, conn)
		go relayConnToFile(name, conn, fd)
	}
}


