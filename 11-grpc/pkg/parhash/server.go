package parhash

import (
	"context"
	"math/rand"
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
	"fs101ex/pkg/workgroup"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
type Server struct {
	conf  Config
	stubs []hashpb.HashSvcClient
	conns []*grpc.ClientConn
	l     net.Listener
	stop  context.CancelFunc
	wg    sync.WaitGroup
	sem   *semaphore.Weighted
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.stubs = make([]hashpb.HashSvcClient, len(s.conf.BackendAddrs))
	s.conns = make([]*grpc.ClientConn, len(s.stubs))
	for i, addr := range s.conf.BackendAddrs {
		s.conns[i], err = grpc.Dial(addr)
		if err != nil {
			return err
		}
		s.stubs[i] = hashpb.NewHashSvcClient(s.conns[i])
	}

	s.wg.Go(func() {
		srv.Serve(s.l)
	})
	s.wg.Go(func() {
		<-ctx.Done()
		s.l.Close()
	})

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
	for _, conn := range s.conns {
		conn.Close()
	}
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	s.wg.Add(1)
	defer s.wg.Done()
	wg := workgroup.New(workgroup.Config{Sem: s.sem})
	b := req.GetData()
	resp = &parhashpb.ParHashResp{}
	resp.Hashes = make([][]byte, len(b))
	for i := range b {
		i := i
		stub := s.stubs[rand.Intn(len(s.stubs))]
		wg.Go(ctx, func(ctx context.Context) error {
			hashResp, err := stub.Hash(ctx, &hashpb.HashReq{Data: b[i]})
			if err != nil {
				return err
			}
			resp.Hashes[i] = hashResp.GetHash()
			return nil
		})
	}
	err = wg.Wait()
	if err != nil {
		return nil, err
	}
	return resp, nil
}
