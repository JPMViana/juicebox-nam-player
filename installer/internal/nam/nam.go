// SPDX-License-Identifier: GPL-3.0-only
// Package nam turns a .nam capture (NAM A2 / A2-Lite) into a Juice-Box capture slot: a port of tools/nam2c.py.
package nam

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"math"
	"path/filepath"
	"strings"
)

// NWeights is the A2-Lite weight count (with head_scale).
const NWeights = 1871

var (
	kernels   = append(append(rep(6, 14), 15, 15), rep(6, 7)...)
	dilations = []int{1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239}
	// The back half (layers 13..22 and the head) runs in fixed point on core 1: weights Q2.29 (|w| < 4). Its first
	// weight in .nam order: rechannel (3), then 13 layers of kernel 6 (9 k + 18 each).
	backStart = 3 + 13*(9*6+18)
)

func rep(v, n int) []int {
	s := make([]int, n)
	for i := range s {
		s[i] = v
	}
	return s
}

type model struct {
	Architecture string          `json:"architecture"`
	Config       json.RawMessage `json:"config"`
	Weights      []float64       `json:"weights"`
	SampleRate   float64         `json:"sample_rate"`
	Metadata     map[string]any  `json:"metadata"`
}

type wavenetCfg struct {
	Layers []struct {
		Channels    int   `json:"channels"`
		Bottleneck  int   `json:"bottleneck"`
		KernelSizes []int `json:"kernel_sizes"`
		Dilations   []int `json:"dilations"`
	} `json:"layers"`
}

// Capture is a checked A2-Lite capture.
type Capture struct {
	Weights    []float32
	SampleRate int
	Norm       float32 // loudness trim to -18 dB, as the NAM plugin
	Title      string
	Loudness   *float64
}

// Error kinds the UI explains.
var (
	ErrNotA2     = errors.New("not a NAM A2 capture (WaveNet A2 / A2-Lite only)")
	ErrFixed     = errors.New("weights too large for the pedal's fixed-point math")
	ErrMalformed = errors.New("not a valid .nam file")
)

func sameInts(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func pickLite(data []byte) (*model, error) {
	var m model
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, fmt.Errorf("%w: %v", ErrMalformed, err)
	}
	if m.Architecture == "SlimmableContainer" { // A2: the submodel with 3 channels is the Lite width
		var c struct {
			Submodels []struct {
				Model json.RawMessage `json:"model"`
			} `json:"submodels"`
		}
		if err := json.Unmarshal(m.Config, &c); err != nil {
			return nil, fmt.Errorf("%w: %v", ErrMalformed, err)
		}
		for _, s := range c.Submodels {
			var sm model
			if json.Unmarshal(s.Model, &sm) != nil || sm.Architecture != "WaveNet" {
				continue
			}
			var wc wavenetCfg
			if json.Unmarshal(sm.Config, &wc) == nil && len(wc.Layers) > 0 && wc.Layers[0].Channels == 3 {
				if sm.SampleRate == 0 {
					sm.SampleRate = m.SampleRate
				}
				if sm.Metadata == nil {
					sm.Metadata = m.Metadata
				}
				return &sm, nil
			}
		}
		return nil, fmt.Errorf("%w: no 3-channel (A2-Lite) submodel", ErrNotA2)
	}
	return &m, nil
}

// Read checks a .nam file and returns the A2-Lite capture in it. title "" = the file name.
func Read(data []byte, path, title string) (*Capture, error) {
	m, err := pickLite(data)
	if err != nil {
		return nil, err
	}
	if m.Architecture != "WaveNet" {
		return nil, fmt.Errorf("%w: architecture %q", ErrNotA2, m.Architecture)
	}
	var wc wavenetCfg
	if err := json.Unmarshal(m.Config, &wc); err != nil || len(wc.Layers) != 1 {
		return nil, fmt.Errorf("%w: A2 has one layer array", ErrNotA2)
	}
	L := wc.Layers[0]
	if L.Channels != 3 || L.Bottleneck != 3 || !sameInts(L.KernelSizes, kernels) || !sameInts(L.Dilations, dilations) {
		return nil, fmt.Errorf("%w: the pedal runs A2-Lite (3 channels, the A2 layout)", ErrNotA2)
	}
	if len(m.Weights) != NWeights {
		return nil, fmt.Errorf("%w: %d weights, A2-Lite has %d", ErrNotA2, len(m.Weights), NWeights)
	}
	c := &Capture{Weights: make([]float32, NWeights), SampleRate: 48000, Norm: 1}
	for i, w := range m.Weights {
		if math.IsNaN(w) || math.IsInf(w, 0) {
			return nil, fmt.Errorf("%w: NaN / inf weights", ErrMalformed)
		}
		if i >= backStart && math.Abs(w) >= 4 {
			return nil, fmt.Errorf("%w (a weight of %.3f)", ErrFixed, w)
		}
		c.Weights[i] = float32(w)
	}
	if m.SampleRate > 0 {
		c.SampleRate = int(m.SampleRate)
	}
	if l, ok := m.Metadata["loudness"].(float64); ok {
		c.Loudness = &l
		c.Norm = float32(math.Pow(10, (-18-l)/20))
	}
	if title == "" {
		title = strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	}
	c.Title = CleanTitle(title)
	return c, nil
}

// CleanTitle keeps what the pedal's font draws in a name: printable ASCII, 23 characters.
func CleanTitle(t string) string {
	var b strings.Builder
	for _, r := range t {
		if b.Len() >= 23 {
			break
		}
		if r >= 32 && r < 127 && r != '"' && r != '\\' {
			b.WriteRune(r)
		} else {
			b.WriteByte('_')
		}
	}
	return b.String()
}

// SlotMagic is "JBM1".
const SlotMagic = 0x314D424A

// Slot is the 64-byte header + the weights (float32 LE, .nam order), as fw v0.14+ stores a capture.
func (c *Capture) Slot() []byte {
	data := make([]byte, 4*len(c.Weights))
	for i, w := range c.Weights {
		binary.LittleEndian.PutUint32(data[4*i:], math.Float32bits(w))
	}
	h := make([]byte, 64)
	binary.LittleEndian.PutUint32(h[0:], SlotMagic)
	binary.LittleEndian.PutUint16(h[4:], 64)
	binary.LittleEndian.PutUint16(h[6:], 1)
	binary.LittleEndian.PutUint32(h[8:], uint32(len(c.Weights)))
	binary.LittleEndian.PutUint32(h[12:], crc32.ChecksumIEEE(data))
	binary.LittleEndian.PutUint32(h[16:], uint32(c.SampleRate))
	binary.LittleEndian.PutUint32(h[20:], math.Float32bits(c.Norm))
	copy(h[24:47], c.Title)
	binary.LittleEndian.PutUint32(h[60:], crc32.ChecksumIEEE(h[:60]))
	return append(h, data...)
}
