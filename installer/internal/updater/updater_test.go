package updater

import (
	"bytes"
	"encoding/binary"
	"testing"
	"time"

	"juicebox/installer/internal/midiio"
	"juicebox/installer/internal/sysex"
)

// a simulated pedal: running firmware -> update loader -> new firmware
type fakePedal struct {
	ident   string // what the handshake answers now
	port    string
	image   []byte
	next    int
	stage   int // 0 running, 1 loader, 2 done
	product string
	bad     int // bytes that differed from the package
	q       [][]byte
}

func identPkt(s string) []byte {
	d := make([]byte, 34)
	copy(d, []byte{0x00, 0x59, 0x11})
	copy(d[6:], s)
	return append(append([]byte{0xF0}, sysex.Pack7(d)...), 0xF7)
}

func reqPkt(addr uint32, n int) []byte {
	u := make([]byte, 15)
	copy(u, []byte{0x00, 0x59, 0x30})
	binary.LittleEndian.PutUint32(u[7:], addr)
	u[11], u[12] = byte(n), byte(n>>8)
	var s byte
	for _, b := range u[6:14] {
		s += b
	}
	u[14] = ^s
	return append(append([]byte{0xF0}, sysex.Pack7(u)...), 0xF7)
}

func (p *fakePedal) request() {
	step := 200
	if p.stage == 0 && p.next >= 2000 || p.next >= len(p.image) { // step 1 reads a part only
		fin := uint32(finishCheck)
		if p.stage == 1 {
			fin = finishWrite
		}
		p.q = append(p.q, reqPkt(fin, 8))
		return
	}
	n := step
	if p.next+n > len(p.image) {
		n = len(p.image) - p.next
	}
	p.q = append(p.q, reqPkt(uint32(p.next), n))
}

func (p *fakePedal) recv(pkt []byte) {
	switch {
	case bytes.Equal(pkt, hsQuery):
		p.q = append(p.q, identPkt(p.ident))
	case bytes.Equal(pkt, upgrade):
		p.next = 0
		p.request()
	default:
		u := sysex.Unpack7(pkt[1 : len(pkt)-1])
		addr := binary.LittleEndian.Uint32(u[7:])
		n := int(u[11]) | int(u[12])<<8
		data := u[14 : 14+n]
		if addr == finishCheck { // -> the loader
			p.stage, p.ident, p.port = 1, "ota-BlackBox_980", "Jieli loader"
			return
		}
		if addr == finishWrite {
			p.stage, p.ident, p.port = 2, p.product, "Juice-Box"
			return
		}
		if !bytes.Equal(data, p.image[addr:int(addr)+n]) {
			p.bad++
		}
		p.next = int(addr) + n
		p.request()
	}
}

type fakeBE struct{ p *fakePedal }

func (b *fakeBE) Inputs() []string  { return []string{b.p.port} }
func (b *fakeBE) Outputs() []string { return []string{b.p.port} }
func (b *fakeBE) Open(in, out string) (midiio.Link, error) {
	return &fakeLink{b.p, in}, nil
}

type fakeLink struct {
	p    *fakePedal
	name string
}

func (l *fakeLink) Name() string         { return l.name }
func (l *fakeLink) Send(pkt []byte) bool { l.p.recv(pkt); return true }
func (l *fakeLink) Read(time.Duration) []byte {
	if len(l.p.q) == 0 || l.p.port != l.name {
		return nil
	}
	x := l.p.q[0]
	l.p.q = l.p.q[1:]
	return x
}
func (l *fakeLink) Drain()     { l.p.q = nil }
func (l *fakeLink) Lost() bool { return l.p.port != l.name }
func (l *fakeLink) Close()     {}

func fast() Delays {
	d := time.Millisecond
	return Delays{Start: d, Reply: 0, Loader: d, Reboot: d, Retry: d, Poll: d, HS: 5 * d, IdleCheck: 50 * d,
		IdleWrite: 50 * d, WaitLoader: 200 * d, WaitReboot: 200 * d}
}

func TestInstall(t *testing.T) {
	img := make([]byte, 7000)
	for i := range img {
		img[i] = byte(i * 7)
	}
	p := &fakePedal{ident: "BlackBox_979", port: "Juice-Box", image: img, product: "BlackBox_980"}
	u := &Updater{BE: &fakeBE{p}, D: fast()}
	dev := u.Find(nil)
	if dev == nil || dev.ID.Text != "BlackBox_979" || dev.ID.Loader() {
		t.Fatalf("find: %+v", dev)
	}
	var steps []string
	got, err := u.Install(dev, img, "BlackBox_980", func(s, _ string, _ int) {
		if len(steps) == 0 || steps[len(steps)-1] != s {
			steps = append(steps, s)
		}
	})
	if err != nil || got != "BlackBox_980" || p.bad != 0 || p.stage != 2 {
		t.Fatalf("install: %q %v bad=%d stage=%d", got, err, p.bad, p.stage)
	}
	want := "start check loader write reboot done"
	if s := joinSteps(steps); s != want {
		t.Fatalf("steps %q", s)
	}
}

func TestWrongModel(t *testing.T) {
	p := &fakePedal{ident: "FM-1_906", port: "Juice-Box", image: make([]byte, 10)}
	u := &Updater{BE: &fakeBE{p}, D: fast()}
	dev := u.Find(nil)
	if _, err := u.Install(dev, p.image, "BlackBox_980", nil); err == nil || err.(*Error).Code != ErrModel {
		t.Fatalf("%v", err)
	}
}

func joinSteps(s []string) string {
	out := ""
	for i, x := range s {
		if i > 0 {
			out += " "
		}
		out += x
	}
	return out
}
