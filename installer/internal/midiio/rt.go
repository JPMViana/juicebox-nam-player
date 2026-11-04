// SPDX-License-Identifier: GPL-3.0-only
package midiio

import (
	"errors"
	"sync"
	"time"

	"gitlab.com/gomidi/midi/v2/drivers"
	"gitlab.com/gomidi/midi/v2/drivers/rtmididrv"
)

// RT is the rtmidi backend.
type RT struct {
	mu  sync.Mutex
	drv *rtmididrv.Driver
}

// NewRT opens the MIDI system.
func NewRT() (*RT, error) {
	d, err := rtmididrv.New()
	if err != nil {
		return nil, err
	}
	return &RT{drv: d}, nil
}

// Close releases the MIDI system.
func (r *RT) Close() { r.drv.Close() }

func (r *RT) Inputs() []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	ins, _ := r.drv.Ins()
	var n []string
	for _, p := range ins {
		n = append(n, p.String())
	}
	return n
}

func (r *RT) Outputs() []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	outs, _ := r.drv.Outs()
	var n []string
	for _, p := range outs {
		n = append(n, p.String())
	}
	return n
}

func (r *RT) Open(inName, outName string) (Link, error) {
	r.mu.Lock()
	var in drivers.In
	var out drivers.Out
	ins, _ := r.drv.Ins()
	for _, p := range ins {
		if p.String() == inName {
			in = p
		}
	}
	outs, _ := r.drv.Outs()
	for _, p := range outs {
		if p.String() == outName {
			out = p
		}
	}
	r.mu.Unlock()
	if in == nil || out == nil {
		return nil, errors.New("MIDI port not found")
	}
	if err := in.Open(); err != nil {
		return nil, err
	}
	if err := out.Open(); err != nil {
		in.Close()
		return nil, err
	}
	l := &rtLink{be: r, name: inName, in: in, out: out, q: make(chan []byte, 256)}
	stop, err := in.Listen(func(msg []byte, _ int32) {
		if len(msg) >= 2 && msg[0] == 0xF0 {
			select {
			case l.q <- append([]byte(nil), msg...):
			default: // a full queue drops: the protocols retry
			}
		}
	}, drivers.ListenConfig{SysEx: true, SysExBufferSize: 4096})
	if err != nil {
		in.Close()
		out.Close()
		return nil, err
	}
	l.stop = stop
	time.Sleep(400 * time.Millisecond) // CoreMIDI may drop a message sent right after opening
	return l, nil
}

type rtLink struct {
	be   *RT
	name string
	in   drivers.In
	out  drivers.Out
	stop func()
	q    chan []byte
}

func (l *rtLink) Name() string { return l.name }

func (l *rtLink) Send(pkt []byte) bool { return l.out.Send(pkt) == nil }

func (l *rtLink) Read(timeout time.Duration) []byte {
	if timeout < 0 {
		timeout = 0
	}
	select {
	case p := <-l.q:
		return p
	case <-time.After(timeout):
		return nil
	}
}

func (l *rtLink) Drain() {
	for {
		select {
		case <-l.q:
		default:
			return
		}
	}
}

func (l *rtLink) Lost() bool {
	for _, n := range l.be.Inputs() {
		if n == l.name {
			return false
		}
	}
	return true
}

func (l *rtLink) Close() {
	if l.stop != nil {
		l.stop()
	}
	l.in.Close()
	l.out.Close()
}
