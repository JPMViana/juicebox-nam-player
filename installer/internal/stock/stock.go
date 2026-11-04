// SPDX-License-Identifier: GPL-3.0-only
// Package stock gets the official V20 package (BlackBox_FACTORY_V20.fwsc) onto the user's machine: it is never
// shipped with the installer (vendor rights); a cached copy is used when its checksum matches.
package stock

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"juicebox/installer/internal/fwbuild"
)

// URLs to try in order. TODO(distribution): an official M-VAVE download, if one can be found and pinned.
var URLs = []string{
	"https://raw.githubusercontent.com/entitymar/annblackbox-custom-firmware-installer/main/firmware/BlackBox_FACTORY_V20.fwsc",
}

const fileName = "BlackBox_FACTORY_V20.fwsc"

// CachePath is where the downloaded package is kept.
func CachePath() (string, error) {
	d, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(d, "Juice-Box", fileName), nil
}

// Load reads path ("" = the cache, downloading it if needed) and checks it is the official V20.
func Load(path string) ([]byte, error) {
	if path != "" {
		b, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		if !fwbuild.IsStock(b) {
			return nil, fmt.Errorf("%s is not the official V20 package (checksum)", path)
		}
		return b, nil
	}
	cp, err := CachePath()
	if err != nil {
		return nil, err
	}
	if b, err := os.ReadFile(cp); err == nil && fwbuild.IsStock(b) {
		return b, nil
	}
	var last error
	cl := &http.Client{Timeout: 60 * time.Second}
	for _, u := range URLs {
		resp, err := cl.Get(u)
		if err != nil {
			last = err
			continue
		}
		b, err := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
		resp.Body.Close()
		if err != nil || resp.StatusCode != 200 {
			last = fmt.Errorf("%s: HTTP %d %v", u, resp.StatusCode, err)
			continue
		}
		if !fwbuild.IsStock(b) {
			last = fmt.Errorf("%s: not the official V20 (checksum)", u)
			continue
		}
		if err := os.MkdirAll(filepath.Dir(cp), 0o755); err == nil {
			os.WriteFile(cp, b, 0o644)
		}
		return b, nil
	}
	return nil, fmt.Errorf("could not download the official firmware: %v", last)
}
