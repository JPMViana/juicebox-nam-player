package jlpkg

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// The vendor package and our build are not in the repository: the tests run where they exist.
var root = filepath.Join("..", "..", "..")

func readOrSkip(t *testing.T, p string) []byte {
	b, err := os.ReadFile(filepath.Join(root, p))
	if err != nil {
		t.Skipf("%s: %v", p, err)
	}
	return b
}

func TestStockRoundTrip(t *testing.T) {
	raw := readOrSkip(t, "firmware/stock/BlackBox_FACTORY_V20.fwsc")
	s, err := Unpack(raw)
	if err != nil {
		t.Fatal(err)
	}
	if s.Product != "BlackBox_020" || s.ChipKey != 0x980F {
		t.Fatalf("product %q key %#x", s.Product, s.ChipKey)
	}
	flash, err := s.BuildFlash(nil, false)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(flash, s.Flash) {
		t.Fatal("rebuilt flash.bin differs from the stock one")
	}
}

// the same package fw/build.py made (tools/bbpkg.py build ... --app build/fw/app.bin --product <its identity>)
func TestSameAsPython(t *testing.T) {
	raw := readOrSkip(t, "firmware/stock/BlackBox_FACTORY_V20.fwsc")
	app := readOrSkip(t, "build/fw/app.bin")
	matches, _ := filepath.Glob(filepath.Join(root, "build/fw/juicebox-BlackBox_*.fwsc"))
	var want []byte
	var product string
	for _, m := range matches { // the one built with this app.bin: the newest
		st, _ := os.Stat(m)
		if want == nil || st.ModTime().After(newest) {
			want, _ = os.ReadFile(m)
			newest = st.ModTime()
		}
	}
	if want == nil {
		t.Skip("no build/fw/juicebox-*.fwsc")
	}
	product, _ = ProductOf(want)
	s, err := Unpack(raw)
	if err != nil {
		t.Fatal(err)
	}
	got, err := s.Package(app, product, false)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		n := 0
		for n < len(got) && n < len(want) && got[n] == want[n] {
			n++
		}
		t.Fatalf("%s: differs from the Python build at %#x (%d vs %d B)", product, n, len(got), len(want))
	}
}

var newest time.Time
