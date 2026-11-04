// SPDX-License-Identifier: GPL-3.0-only
// Package midiio: MIDI ports as SysEx links. The updater and the capture store talk to a Link, so they can be
// tested against a simulated pedal; RT is the real backend (rtmidi: CoreMIDI on macOS, WinMM on Windows).
package midiio

import (
	"regexp"
	"time"
)

// Backend lists and opens ports. Names are listed again on every call (the pedal reboots during an install).
type Backend interface {
	Inputs() []string
	Outputs() []string
	Open(in, out string) (Link, error)
}

// Link is one input / output pair carrying complete SysEx messages (F0 .. F7).
type Link interface {
	Name() string
	Send(pkt []byte) bool
	Read(timeout time.Duration) []byte // nil on timeout
	Drain()
	Lost() bool // the input port is gone
	Close()
}

var trailingNum = regexp.MustCompile(`\s+\d+$`)

// PairOutput finds the output port of an input port (Windows numbers each port).
func PairOutput(in string, outs []string) string {
	for _, o := range outs {
		if o == in {
			return o
		}
	}
	base := trailingNum.ReplaceAllString(in, "")
	var same []string
	for _, o := range outs {
		if trailingNum.ReplaceAllString(o, "") == base {
			same = append(same, o)
		}
	}
	if len(same) == 1 {
		return same[0]
	}
	if len(outs) == 1 {
		return outs[0]
	}
	return ""
}
