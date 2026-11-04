package fwbuild

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"juicebox/installer/internal/jlpkg"
)

var root = filepath.Join("..", "..", "..")

func read(t *testing.T, p string) []byte {
	b, err := os.ReadFile(filepath.Join(root, p))
	if err != nil {
		t.Skipf("%s: %v", p, err)
	}
	return b
}

// release app.bin + manifest + V20 == the full package fw/build.py made
func TestAssembleSameAsBuild(t *testing.T) {
	stock := read(t, "firmware/stock/BlackBox_FACTORY_V20.fwsc")
	mans, _ := filepath.Glob(filepath.Join(root, "build/fw/release/juicebox-*.json"))
	if len(mans) == 0 {
		t.Skip("no release build")
	}
	mb, _ := os.ReadFile(mans[len(mans)-1])
	m, err := ParseManifest(mb)
	if err != nil {
		t.Fatal(err)
	}
	app := read(t, "build/fw/release/"+m.App)
	want := read(t, "build/fw/juicebox-"+m.Product+".fwsc")
	got, err := Assemble(stock, app, m, false)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Fatal("assembled package differs from fw/build.py's")
	}
	if p, _ := jlpkg.ProductOf(got); p != m.Product {
		t.Fatalf("identity %q", p)
	}
	if _, err := Assemble(stock[:len(stock)-1], app, m, false); err == nil {
		t.Fatal("a damaged stock package was accepted")
	}
}
