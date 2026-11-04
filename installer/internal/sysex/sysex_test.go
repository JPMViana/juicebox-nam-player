package sysex

import (
	"bytes"
	"testing"
)

func TestRoundTrip(t *testing.T) {
	for n := 0; n < 300; n++ {
		d := make([]byte, n)
		for i := range d {
			d[i] = byte(i*37 + n)
		}
		p := Pack7(d)
		for _, b := range p {
			if b > 0x7F {
				t.Fatal("not 7-bit")
			}
		}
		if got := Unpack7(p); !bytes.Equal(got[:n], d) || len(got) != n {
			t.Fatalf("n=%d: %x != %x", n, got, d)
		}
	}
	// the same bit stream as tools/bb_install.py pack7
	if got := Pack7([]byte{0xFF, 0x01}); !bytes.Equal(got, []byte{0x7F, 0x03, 0x00}) {
		t.Fatalf("%x", got)
	}
}
