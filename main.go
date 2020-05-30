package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"gvisor.dev/gvisor/pkg/abi/linux"
	"gvisor.dev/gvisor/pkg/sentry/arch"
	"gvisor.dev/gvisor/pkg/context"
	"gvisor.dev/gvisor/pkg/fspath"
	"gvisor.dev/gvisor/pkg/sentry/vfs"
	"gvisor.dev/gvisor/pkg/sentry/kernel/auth"
	"gvisor.dev/gvisor/pkg/sentry/fsimpl/devpts"
	"gvisor.dev/gvisor/pkg/usermem"
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
	
	doRelay("back", func () (*vfs.FileDescription, error) {
		fd, err := vfsObj.OpenAt(ctx, auth.CredentialsFromContext(ctx),
			&backPath,
			&vfs.OpenOptions{})
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
				&vfs.OpenOptions{})
			return frontFd, err
		})
		return fd, nil
	});
}

func relayFileToConn(fd *vfs.FileDescription, conn net.Conn) {
	buf := make([]byte, 4096)
	for {
		n, err := fd.Read(ctx, usermem.BytesIOSequence(buf), vfs.ReadOptions{})
		if err != nil {
			log.Printf("Error reading %d from fd: %s",n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		log.Printf("Read %d from fd: %s",n, buf[0:m])
		for n > 0 {
			written, err := conn.Write(buf[0:n])
			if err != nil {
				log.Printf("Error writing to conn:%s", err) 
				return;
			}
			n -= int64(written);
			log.Printf("Wrote %d, first %d, %d more", written, buf[0], n) 
			buf = buf[written:]
		}
	}
}
func relayConnToFile(conn net.Conn, fd *vfs.FileDescription) {
	buf := make([]byte, 4096)
	for {
		n, err := conn.Read(buf)
		if err != nil {
			log.Printf("Error reading %d from conn: %s",n, err)
			return;
		}
		m := n;
		if m > 40 {
			m = 40;
		}
		log.Printf("Read %d from conn: %s",n, buf[0:m])
		for n > 0 {
			written, err := fd.Write(ctx, usermem.BytesIOSequence(buf), 
				vfs.WriteOptions{})
			if err != nil {
				log.Printf("Error writing to file:%s", err) 
				return;
			}
			n -= int(written);
			log.Printf("Wrote %d, first %d, %d more", written, buf[0], n) 
			buf = buf[written:]
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
		backFd, err := fdGet()
		if err != nil {
			log.Fatalf("error on fdGet(): ", err)
		}
		
		//errch := make(chan error)
		go relayFileToConn(backFd, conn)
		go relayConnToFile(conn, backFd)
	}
}




