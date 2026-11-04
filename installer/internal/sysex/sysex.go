// SPDX-License-Identifier: GPL-3.0-only
// Package sysex: the 7-bit packing the JieLi update protocol and Juice-Box use (LSB first bit stream).
package sysex

// Pack7 spreads bytes over 7-bit MIDI data bytes.
func Pack7(data []byte) []byte {
	out := make([]byte, 0, len(data)*8/7+1)
	var acc uint32
	nb := 0
	for _, b := range data {
		acc |= uint32(b) << nb
		nb += 8
		for nb >= 7 {
			out = append(out, byte(acc&0x7F))
			acc >>= 7
			nb -= 7
		}
	}
	if nb > 0 {
		out = append(out, byte(acc&0x7F))
	}
	return out
}

// Unpack7 is the inverse of Pack7.
func Unpack7(s []byte) []byte {
	out := make([]byte, 0, len(s)*7/8)
	var acc uint32
	nb := 0
	for _, b := range s {
		acc |= uint32(b&0x7F) << nb
		nb += 7
		for nb >= 8 {
			out = append(out, byte(acc))
			acc >>= 8
			nb -= 8
		}
	}
	return out
}
