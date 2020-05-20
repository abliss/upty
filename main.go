package main

import (
    "encoding/binary"
	"flag"
	"log"
	"net"
	"os"
)

var (
	//path = flag.String("path", "", "Path to directory to serve")
	//addr = flag.String("socket", "", "Path to unix socket to listen on")
)

func main() {
	flag.Parse()
	for {
		m2s := make(chan []byte)
		s2m := make(chan []byte)
		errch := make(chan error)

		go doRelay("back", m2s, s2m, errch)
		go doRelay("front", s2m, m2s, errch)
		err := <- errch
		log.Printf("Error ", err)
		os.Remove("back");
		os.Remove("front");
	}
}


func relayConnToCh (errch chan error, ch chan []byte, conn net.Conn) {
	buf := make([]byte, 4096)
	flag := make([]byte, 5)
	for {
		n, err := conn.Read(buf)
		if err != nil {
			errch <- err
			continue;
		}
		log.Printf("Read %d from conn",n) 
		flag[0] = 0;
		binary.LittleEndian.PutUint32(flag[1:], uint32(n))
		ch <- flag
		ch <- buf[0:n]
	}
}

func relayChToConn (errch chan error, ch chan []byte, conn net.Conn) {
	for {
		flag := <- ch
		_ = flag
		buf := <- ch
		left := len(buf)
		for (left > 0) {
			n, err := conn.Write(buf)
			if err != nil {
				errch <- err
				break;
			}
			log.Printf("Wrote %d",n) 
			left -= n
			buf = buf[n:]
		}
	}
}

func doRelay(name string, m2s, s2m chan []byte, errch chan error) {
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
		go relayConnToCh(errch, m2s, conn)
		go relayChToConn(errch, s2m, conn)
	}
}

