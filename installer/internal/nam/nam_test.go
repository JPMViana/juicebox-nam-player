package nam

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// the same slot image as tools/nam2c.py --slot
func TestSameAsPython(t *testing.T) {
	root := filepath.Join("..", "..", "..")
	src := filepath.Join(root, "reference/NeuralAmpModelerCore/example_models/A2.nam")
	data, err := os.ReadFile(src)
	if err != nil {
		t.Skip(err)
	}
	out := filepath.Join(t.TempDir(), "a2.jbm")
	cmd := exec.Command("python3", "-I", filepath.Join(root, "tools/nam2c.py"), src, "--slot", out, "--title", "A2 test")
	if b, err := cmd.CombinedOutput(); err != nil {
		t.Skipf("python: %v %s", err, b)
	}
	want, _ := os.ReadFile(out)
	c, err := Read(data, src, "A2 test")
	if err != nil {
		t.Fatal(err)
	}
	if got := c.Slot(); !bytes.Equal(got, want) {
		n := 0
		for n < len(got) && got[n] == want[n] {
			n++
		}
		t.Fatalf("differs at %d (%d vs %d B)", n, len(got), len(want))
	}
}

func TestRejects(t *testing.T) {
	if _, err := Read([]byte(`{"architecture":"LSTM","config":{},"weights":[]}`), "x.nam", ""); err == nil {
		t.Fatal("LSTM accepted")
	}
	if _, err := Read([]byte(`not json`), "x.nam", ""); err == nil {
		t.Fatal("garbage accepted")
	}
}
