// SPDX-License-Identifier: GPL-3.0-only
// Package fwbuild assembles an installable Juice-Box package on the user's machine: the release app.bin
// (which ships without any vendor bytes) + the official V20 package the user's copy of the installer
// downloads. The official update loader's body is put back into the app (Juice-Box keeps it as its
// rescue copy), then the stock flash layout and loader wrap it, as fw/build.py does.
package fwbuild

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"

	"juicebox/installer/internal/jlpkg"
)

// StockSHA256 is the official BlackBox_FACTORY_V20.fwsc.
const StockSHA256 = "c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08"

// Manifest is the release's juicebox-<version>.json (fw/build.py --release).
type Manifest struct {
	Format      int    `json:"format"`
	Firmware    string `json:"firmware"`
	Version     string `json:"version"`
	Product     string `json:"product"`
	App         string `json:"app"`
	AppSHA256   string `json:"app_sha256"`
	StockSHA256 string `json:"stock_sha256"`
	Loader      struct {
		Offset       int    `json:"offset"`
		Length       int    `json:"length"`
		CRC16        uint16 `json:"crc16"`
		Source       string `json:"source"`
		SourceOffset int    `json:"source_offset"`
	} `json:"loader"`
}

// ParseManifest reads and checks a release manifest.
func ParseManifest(b []byte) (*Manifest, error) {
	var m Manifest
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("manifest: %w", err)
	}
	if m.Format != 1 || m.Firmware != "Juice-Box" || m.Loader.Source != "ota.bin" || m.Loader.Length <= 0 {
		return nil, fmt.Errorf("manifest: not a Juice-Box release manifest (format %d)", m.Format)
	}
	return &m, nil
}

func sum(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:])
}

// IsStock: raw is the unmodified official V20 package.
func IsStock(raw []byte) bool { return sum(raw) == StockSHA256 }

// Assemble builds the .fwsc to install. bumpCfg: needed when the pedal runs the stock firmware (it refuses a
// package whose cfg header equals its own); harmless otherwise.
func Assemble(stockRaw, app []byte, m *Manifest, bumpCfg bool) ([]byte, error) {
	if sum(stockRaw) != m.StockSHA256 {
		return nil, fmt.Errorf("the stock package is not the official V20 this release was built for")
	}
	if sum(app) != m.AppSHA256 {
		return nil, fmt.Errorf("%s: checksum mismatch (damaged download?)", m.App)
	}
	s, err := jlpkg.Unpack(stockRaw)
	if err != nil {
		return nil, err
	}
	l := m.Loader
	if l.SourceOffset+l.Length > len(s.OTA) || l.Offset+l.Length > len(app) {
		return nil, fmt.Errorf("manifest: the loader does not fit")
	}
	body := s.OTA[l.SourceOffset : l.SourceOffset+l.Length]
	if jlpkg.CRC16(body, 0) != l.CRC16 {
		return nil, fmt.Errorf("the stock update loader is not the expected one")
	}
	if !bytes.Equal(app[l.Offset:l.Offset+l.Length], make([]byte, l.Length)) {
		return nil, fmt.Errorf("%s: the loader region is not empty", m.App)
	}
	full := append([]byte(nil), app...)
	copy(full[l.Offset:], body)
	return s.Package(full, m.Product, bumpCfg)
}
