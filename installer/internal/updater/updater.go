// SPDX-License-Identifier: GPL-3.0-only
// Package updater installs a package (.fwsc) on an M-VAVE ANN BlackBox over USB-MIDI: a port of
// tools/bb_install.py (derived from Felucca's fm1_install.py, Leo Kuroshita / Hügelton Instruments, GPL-3.0).
//
// Step 1: the running firmware reads parts of the package (SysEx read requests on the logical image) and starts
// the update loader. Step 2: the loader reads the whole package and writes it. Then the pedal restarts and the
// installed identity is checked. A pedal left in update mode (an install cut off) only needs step 2.
package updater

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"regexp"
	"strings"
	"time"

	"juicebox/installer/internal/midiio"
	"juicebox/installer/internal/sysex"
)

var (
	hsQuery = []byte{0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7}
	upgrade = []byte{0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7}
)

const (
	finishCheck = 0xE0000000
	finishWrite = 0xF0000000
	maxData     = 512
	Device      = "BlackBox"
)

// PortRE matches the MIDI port names the pedal shows up as (stock firmware, its loader, Juice-Box).
var PortRE = regexp.MustCompile(`(?i)juice|armaria|blackbox|black box|ann|sinco|m-vave|mvave|ota|composite|usb-midi|jieli`)

// Delays (seconds in bb_install.py); the tests shorten them.
type Delays struct {
	Start, Reply, Loader, Reboot, Retry, Poll, HS, IdleCheck, IdleWrite, WaitLoader, WaitReboot time.Duration
}

var Default = Delays{Start: 2 * time.Second, Reply: 10 * time.Millisecond, Loader: 3 * time.Second,
	Reboot: 3 * time.Second, Retry: time.Second, Poll: time.Second, HS: time.Second, IdleCheck: 8 * time.Second,
	IdleWrite: 180 * time.Second, WaitLoader: 30 * time.Second, WaitReboot: 40 * time.Second}

// Error codes, for the UI's messages.
type Code int

const (
	ErrNotFound Code = iota + 1
	ErrModel
	ErrLost
	ErrStopped
	ErrBadRequest
	ErrNoLoader
	ErrNoReturn
	ErrMismatch
)

// Error is an install failure; Code tells the UI what to say and suggest.
type Error struct {
	Code Code
	Msg  string
}

func (e *Error) Error() string { return e.Msg }

// Identity is what the handshake answers: "BlackBox_980", or "ota-..." for the update loader.
type Identity struct {
	Text    string
	Model   string
	Version int
}

// Loader: the pedal is in update mode.
func (i Identity) Loader() bool { return strings.HasPrefix(strings.ToLower(i.Model), "ota-") }

// ModelOf: "BlackBox" from "BlackBox_980" or "ota-BlackBox_980".
func ModelOf(text string) string { return strings.TrimPrefix(strings.SplitN(text, "_", 2)[0], "ota-") }

var identRE = regexp.MustCompile(`^([^_]+)_(\d+)$`)

func response(addr uint32, data []byte, fl byte) []byte {
	n := len(data)
	body := []byte{0x00, 0x59, 0x30, byte(n + 8), byte((n + 8) >> 8), 0, fl}
	body = binary.LittleEndian.AppendUint32(body, addr)
	body = append(body, byte(n), byte(n>>8), 0)
	body = append(body, data...)
	var s byte
	for _, b := range body[6:] {
		s += b
	}
	body = append(body, ^s)
	return append(append([]byte{0xF0}, sysex.Pack7(body)...), 0xF7)
}

func parseRequest(pkt []byte) (fl byte, addr uint32, n int, ok bool) {
	if len(pkt) < 2 || pkt[0] != 0xF0 || pkt[len(pkt)-1] != 0xF7 {
		return
	}
	u := sysex.Unpack7(pkt[1 : len(pkt)-1])
	if len(u) != 15 || !bytes.Equal(u[:3], []byte{0x00, 0x59, 0x30}) {
		return
	}
	var s byte
	for _, b := range u[6:14] {
		s += b
	}
	if ^s != u[14] {
		return
	}
	return u[6], binary.LittleEndian.Uint32(u[7:]), int(u[11]) | int(u[12])<<8 | int(u[13])<<16, true
}

func printable(b []byte) string {
	var sb strings.Builder
	for _, c := range b {
		if c >= 32 && c < 127 {
			sb.WriteByte(c)
		} else {
			sb.WriteByte('?')
		}
	}
	return sb.String()
}

func parseIdentity(pkt []byte) (Identity, bool) {
	if len(pkt) < 2 || pkt[0] != 0xF0 || pkt[len(pkt)-1] != 0xF7 {
		return Identity{}, false
	}
	d := sysex.Unpack7(pkt[1 : len(pkt)-1])
	if len(d) != 34 || !bytes.Equal(d[:3], []byte{0x00, 0x59, 0x11}) {
		return Identity{}, false
	}
	txt := printable(bytes.TrimRight(d[6:33], "\x00"))
	if txt == "" { // the official loader started from a RAM-only update record (Juice-Box) answers nothing
		return Identity{"ota-?", "ota-?", 0}, true
	}
	m := identRE.FindStringSubmatch(txt)
	if m == nil {
		return Identity{}, false
	}
	v := 0
	fmt.Sscan(m[2], &v)
	return Identity{txt, m[1], v}, true
}

// Dev is a found pedal with its open link.
type Dev struct {
	Link midiio.Link
	ID   Identity
}

// Step reports progress: "start" (identity), "check" (reads), "loader", "write" (percent), "reboot", "done".
type Step func(step string, arg string, n int)

// Updater finds the pedal and installs.
type Updater struct {
	BE   midiio.Backend
	Port string // part of a port name, "" = by name pattern
	D    Delays
}

func New(be midiio.Backend) *Updater { return &Updater{BE: be, D: Default} }

func (u *Updater) handshake(l midiio.Link, tries int) (Identity, bool) {
	l.Drain()
	for t := 0; t < tries; t++ {
		if !l.Send(hsQuery) {
			return Identity{}, false
		}
		end := time.Now().Add(u.D.HS)
		for time.Now().Before(end) {
			p := l.Read(time.Until(end))
			if p == nil {
				break
			}
			if id, ok := parseIdentity(p); ok {
				return id, true
			}
		}
	}
	return Identity{}, false
}

func (u *Updater) candidates(onlyPort bool) []string {
	var mine, rest []string
	for _, n := range u.BE.Inputs() {
		switch {
		case u.Port != "" && strings.Contains(strings.ToLower(n), strings.ToLower(u.Port)):
			mine = append(mine, n)
		case PortRE.MatchString(n):
			rest = append(rest, n)
		}
	}
	if u.Port == "" {
		return rest
	}
	if onlyPort {
		return mine
	}
	return append(mine, rest...)
}

// Find opens the first matching port that answers the handshake (and want, if given).
func (u *Updater) Find(want func(Identity) bool) *Dev { return u.find(want, true) }

func (u *Updater) find(want func(Identity) bool, onlyPort bool) *Dev {
	outs := u.BE.Outputs()
	for _, name := range u.candidates(onlyPort) {
		out := midiio.PairOutput(name, outs)
		if out == "" {
			continue
		}
		l, err := u.BE.Open(name, out)
		if err != nil {
			continue
		}
		if id, ok := u.handshake(l, 2); ok && (want == nil || want(id)) {
			return &Dev{l, id}
		}
		l.Close()
	}
	return nil
}

func (u *Updater) waitFor(want func(Identity) bool, d time.Duration) *Dev {
	end := time.Now().Add(d)
	for time.Now().Before(end) {
		if dev := u.find(want, false); dev != nil {
			return dev
		}
		time.Sleep(u.D.Retry)
	}
	return nil
}

// serve answers read requests until the device asks for finish.
func (u *Updater) serve(l midiio.Link, image []byte, finish uint32, idle time.Duration, prog func(served, end int)) (served int, finished, lost bool, err error) {
	last := time.Now()
	for {
		p := l.Read(u.D.Poll)
		if p == nil {
			if l.Lost() {
				return served, false, true, nil
			}
			if time.Since(last) > idle {
				return served, false, false, nil
			}
			continue
		}
		fl, addr, n, ok := parseRequest(p)
		if !ok {
			continue
		}
		last = time.Now()
		if addr == finishCheck || addr == finishWrite {
			l.Send(response(addr, []byte("success\x00"), 0))
			if addr == finish {
				return served, true, false, nil
			}
			continue
		}
		if n > maxData || int(addr)+n > len(image) {
			return served, false, false, &Error{ErrBadRequest, fmt.Sprintf("the device asked for %#x+%d, outside the package", addr, n)}
		}
		time.Sleep(u.D.Reply)
		if !l.Send(response(addr, image[addr:int(addr)+n], fl)) {
			return served, false, true, nil
		}
		served++
		prog(served, int(addr)+n)
	}
}

func (u *Updater) check(dev *Dev, image []byte, step Step) (*Dev, error) {
	step("start", dev.ID.Text, 0)
	dev.Link.Send(upgrade)
	time.Sleep(u.D.Start)
	served, finished, lost, err := u.serve(dev.Link, image, finishCheck, u.D.IdleCheck, func(k, _ int) { step("check", "", k) })
	dev.Link.Close()
	if err != nil {
		return nil, err
	}
	if !finished {
		code, what := ErrStopped, "stopped answering"
		if lost {
			code, what = ErrLost, "was disconnected"
		}
		return nil, &Error{code, fmt.Sprintf("the pedal %s after %d requests; nothing was written", what, served)}
	}
	step("loader", "", 0)
	time.Sleep(u.D.Loader)
	ota := u.waitFor(func(i Identity) bool { return i.Loader() }, u.D.WaitLoader)
	if ota == nil {
		return nil, &Error{ErrNoLoader, "the update loader did not appear: replug the USB cable and install again " +
			"(the pedal stays in update mode until it is done)"}
	}
	return ota, nil
}

func (u *Updater) write(ota *Dev, image []byte, step Step) error {
	step("write", "", 0)
	ota.Link.Send(upgrade)
	time.Sleep(u.D.Start)
	served, finished, lost, err := u.serve(ota.Link, image, finishWrite, u.D.IdleWrite, func(_, end int) {
		p := end * 100 / len(image)
		if p > 99 {
			p = 99
		}
		step("write", "", p)
	})
	ota.Link.Close()
	if err != nil {
		return err
	}
	if !finished {
		code, what := ErrStopped, "stopped answering"
		if lost {
			code, what = ErrLost, "was disconnected"
		}
		return &Error{code, fmt.Sprintf("the loader %s after %d requests: replug the USB cable and install again to finish", what, served)}
	}
	step("write", "", 100)
	return nil
}

func (u *Updater) verify(product string, step Step) (string, error) {
	step("reboot", "", 0)
	time.Sleep(u.D.Reboot)
	back := u.waitFor(func(i Identity) bool { return !i.Loader() }, u.D.WaitReboot)
	if back == nil {
		return "", &Error{ErrNoReturn, "the pedal did not come back: power-cycle it"}
	}
	back.Link.Close()
	if back.ID.Text != product {
		return back.ID.Text, &Error{ErrMismatch, fmt.Sprintf("written, but the pedal reports %s (expected %s)", back.ID.Text, product)}
	}
	step("done", back.ID.Text, 0)
	return back.ID.Text, nil
}

// Install writes raw (a .fwsc) to dev (from Find), whose link it takes over.
func (u *Updater) Install(dev *Dev, logical []byte, product string, step Step) (string, error) {
	if step == nil {
		step = func(string, string, int) {}
	}
	if dev.ID.Text != "ota-?" && ModelOf(dev.ID.Text) != ModelOf(product) {
		dev.Link.Close()
		return "", &Error{ErrModel, fmt.Sprintf("the device is %s, the package is for %s", dev.ID.Text, product)}
	}
	ota := dev
	if !dev.ID.Loader() {
		var err error
		if ota, err = u.check(dev, logical, step); err != nil {
			return "", err
		}
	}
	if err := u.write(ota, logical, step); err != nil {
		return "", err
	}
	return u.verify(product, step)
}
