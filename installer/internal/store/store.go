// SPDX-License-Identifier: GPL-3.0-only
// Package store talks to Juice-Box's capture store and banks over USB (fw v0.14+): a port of tools/jb_models.py.
// The protocol is in fw/src/juicebox.c ("capture store"): SysEx F0 7D 'J' 'B' cmd ... F7, replies
// F0 7D 'J' 'B' cmd slot rc F7 (rc 0 = ok).
package store

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"math"
	"time"

	"juicebox/installer/internal/midiio"
	"juicebox/installer/internal/sysex"
)

const (
	Slots  = 8
	None   = 0x7F // no capture
	Banks  = 6
	chunk  = 256
	bkMagc = 0x314B424A // "JBK1"
)

// BankNames are the buttons the banks are on.
var BankNames = [Banks]string{"FX", "AMP", "MOD", "DLY", "REV", "CAB"}

// RC is a pedal error code.
type RC int

func (r RC) Error() string {
	switch r {
	case 1:
		return "invalid request"
	case 2:
		return "flash read error"
	case 3:
		return "capture checksum mismatch"
	case 4:
		return "model rejected"
	case 5:
		return "flash erase error"
	case 6:
		return "flash write error"
	case 7:
		return "invalid header"
	}
	return fmt.Sprintf("error %d", int(r))
}

// ErrNoAnswer: the pedal does not answer a store command (not Juice-Box v0.14+, or busy).
type ErrNoAnswer struct{ Cmd byte }

func (e ErrNoAnswer) Error() string {
	return fmt.Sprintf("no answer to '%c' (is the pedal running Juice-Box?)", e.Cmd)
}

// Slot is one stored capture.
type Slot struct {
	Valid bool
	Title string
	Rate  int
	Norm  float32
}

// Dir is the store: its slots, the running capture, live switching.
type Dir struct {
	Slots  [Slots]Slot
	Active int // slot, None
	Proto  int
	XIP    bool
	Bank   int
}

// Bank is one bank: captures A / B (slot or None), IN / OUT in half dB.
type Bank struct {
	Cap         [2]int
	InHD, OutHD int
}

// Banks is the bank record (64 B on the pedal).
type BankRec struct {
	Bank, AB, Lang int
	B              [Banks]Bank
}

// Client talks to one pedal over a link.
type Client struct {
	L       midiio.Link
	Timeout time.Duration
}

func New(l midiio.Link) *Client { return &Client{L: l, Timeout: 5 * time.Second} }

func (c *Client) send(body ...byte) bool {
	return c.L.Send(append(append([]byte{0xF0, 0x7D, 'J', 'B'}, body...), 0xF7))
}

// reply waits for the store's reply to cmd: its bytes after the command letter.
func (c *Client) reply(cmd byte, timeout time.Duration) ([]byte, error) {
	end := time.Now().Add(timeout)
	for time.Now().Before(end) {
		p := c.L.Read(time.Until(end))
		if p == nil {
			break
		}
		if len(p) > 6 && bytes.Equal(p[:4], []byte{0xF0, 0x7D, 'J', 'B'}) && p[4] == cmd {
			return p[5 : len(p)-1], nil
		}
	}
	return nil, ErrNoAnswer{cmd}
}

// call sends a command and checks its rc; tries > 1 only for idempotent commands.
func (c *Client) call(timeout time.Duration, tries int, cmd byte, args ...byte) ([]byte, error) {
	var r []byte
	var err error
	for k := 0; k < tries; k++ {
		c.L.Drain() // a late reply to an earlier try
		c.send(append([]byte{cmd}, args...)...)
		if r, err = c.reply(cmd, timeout); err == nil {
			break
		}
	}
	if err != nil {
		return nil, err
	}
	if len(r) >= 2 && r[1] != 0 {
		return nil, RC(r[1])
	}
	return r, nil
}

// Directory lists the slots.
func (c *Client) Directory() (*Dir, error) {
	c.L.Drain()
	c.send('d')
	d := &Dir{Active: None}
	for {
		r, err := c.reply('d', c.Timeout)
		if err != nil {
			return nil, err
		}
		if r[0] == 0x7F {
			d.Active = int(r[1])
			if len(r) > 2 {
				d.Proto = int(r[2])
			}
			if len(r) > 3 {
				d.XIP = r[3] != 0
			}
			if len(r) > 4 {
				d.Bank = int(r[4])
			}
			return d, nil
		}
		i := int(r[0])
		if i >= Slots || len(r) < 3 {
			continue
		}
		h := sysex.Unpack7(r[2:])
		if r[1] == 0 || len(h) < 48 {
			continue
		}
		t := h[24:48]
		if j := bytes.IndexByte(t, 0); j >= 0 {
			t = t[:j]
		}
		d.Slots[i] = Slot{true, string(t), int(binary.LittleEndian.Uint32(h[16:])),
			math.Float32frombits(binary.LittleEndian.Uint32(h[20:]))}
	}
}

// Put writes a capture slot image (nam.Capture.Slot) to slot 0..7: erase, the weights, the header last, check.
// The NAM pauses meanwhile; Resume (or Select) reboots the pedal back to playing.
func (c *Client) Put(slot int, image []byte, progress func(pct int)) error {
	if _, err := c.call(10*time.Second, 1, 'e', byte(slot)); err != nil {
		return err
	}
	type part struct {
		off  int
		data []byte
	}
	var parts []part
	for i := 64; i < len(image); i += chunk {
		end := i + chunk
		if end > len(image) {
			end = len(image)
		}
		parts = append(parts, part{i, image[i:end]})
	}
	parts = append(parts, part{0, image[:64]}) // the header last: a slot reads valid only once complete
	for k, p := range parts {
		args := append([]byte{byte(slot), byte(p.off & 0x7F), byte(p.off >> 7 & 0x7F), byte(p.off >> 14 & 0x7F)},
			sysex.Pack7(p.data)...)
		if _, err := c.call(2*time.Second, 3, 'w', args...); err != nil { // rewriting the same bytes is harmless
			return err
		}
		if progress != nil {
			progress((k + 1) * 100 / len(parts))
		}
	}
	_, err := c.call(10*time.Second, 1, 'c', byte(slot))
	return err
}

// Erase empties a slot (the NAM pauses; Resume reboots back to playing).
func (c *Client) Erase(slot int) error {
	_, err := c.call(10*time.Second, 1, 'e', byte(slot))
	return err
}

// Select puts slot on the running bank position and saves; the pedal reboots.
func (c *Client) Select(slot int) error {
	_, err := c.call(15*time.Second, 1, 's', byte(slot))
	return err
}

// Wipe erases every slot and the settings (before going back to the stock firmware).
func (c *Client) Wipe() error { _, err := c.call(30*time.Second, 1, 'x'); return err }

// Resume reboots the pedal back to playing.
func (c *Client) Resume() error { _, err := c.call(5*time.Second, 1, 'r'); return err }

func crcRec(b []byte) uint32 { return crc32.ChecksumIEEE(b[:60]) }

// Banks reads the bank record.
func (c *Client) Banks() (*BankRec, error) {
	c.L.Drain()
	c.send('t')
	r, err := c.reply('t', c.Timeout)
	if err != nil {
		return nil, err
	}
	b := sysex.Unpack7(r)
	if len(b) < 64 || binary.LittleEndian.Uint32(b) != bkMagc {
		return nil, fmt.Errorf("bank record: not understood")
	}
	rec := &BankRec{Bank: int(b[4]), AB: int(b[5]), Lang: int(b[6])}
	for i := 0; i < Banks; i++ {
		o := 8 + 4*i
		rec.B[i] = Bank{[2]int{int(b[o]), int(b[o+1])}, int(int8(b[o+2])), int(int8(b[o+3]))}
	}
	return rec, nil
}

// SetBanks writes a bank record; the pedal saves it and reboots.
func (c *Client) SetBanks(rec *BankRec) error {
	b := make([]byte, 64)
	binary.LittleEndian.PutUint32(b, bkMagc)
	b[4], b[5], b[6] = byte(rec.Bank), byte(rec.AB), byte(rec.Lang)
	for i, bk := range rec.B {
		o := 8 + 4*i
		b[o], b[o+1], b[o+2], b[o+3] = byte(bk.Cap[0]), byte(bk.Cap[1]), byte(int8(bk.InHD)), byte(int8(bk.OutHD))
	}
	binary.LittleEndian.PutUint32(b[60:], crcRec(b))
	_, err := c.call(15*time.Second, 1, 'u', sysex.Pack7(b)...)
	return err
}
