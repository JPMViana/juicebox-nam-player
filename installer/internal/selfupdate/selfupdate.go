// SPDX-License-Identifier: GPL-3.0-only

// Package selfupdate asks GitHub for the latest published Juice-Box release, so the installer can tell its user that a newer
// one exists (it never downloads or runs anything itself: the user gets the release page).
package selfupdate

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"
)

// Repo is where the releases are published.
const Repo = "JPMViana/juicebox-nam-player"

// API is the endpoint for the latest published (not draft, not pre-release) release.
var API = "https://api.github.com/repos/" + Repo + "/releases/latest"

// Info is the result of a check.
type Info struct {
	Available bool   `json:"available"`
	Current   string `json:"current"`
	Latest    string `json:"latest"`
	URL       string `json:"url"`   // the release page
	Notes     string `json:"notes"` // its description (markdown), trimmed
}

// Check compares current ("v0.17.0") with the latest release's tag.
func Check(ctx context.Context, current string) (Info, error) {
	in := Info{Current: current}
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, API, nil)
	if err != nil {
		return in, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "Juice-Box-installer/"+strings.TrimPrefix(current, "v"))
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return in, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return in, fmt.Errorf("GitHub answered %s", resp.Status)
	}
	var r struct {
		Tag  string `json:"tag_name"`
		URL  string `json:"html_url"`
		Body string `json:"body"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&r); err != nil {
		return in, err
	}
	in.Latest, in.URL = r.Tag, r.URL
	if len(r.Body) > 2000 {
		r.Body = r.Body[:2000]
	}
	in.Notes = r.Body
	in.Available = Newer(r.Tag, current)
	return in, nil
}

// Newer reports whether version a ("v1.2.3", a suffix such as "-dev" ignored) is newer than b.
func Newer(a, b string) bool {
	pa, pb := parse(a), parse(b)
	for i := 0; i < 3; i++ {
		if pa[i] != pb[i] {
			return pa[i] > pb[i]
		}
	}
	return false
}

func parse(v string) [3]int {
	var out [3]int
	v = strings.TrimPrefix(strings.TrimSpace(v), "v")
	if i := strings.IndexAny(v, "-+ "); i >= 0 {
		v = v[:i]
	}
	for i, p := range strings.SplitN(v, ".", 3) {
		n, _ := strconv.Atoi(p)
		out[i] = n
	}
	return out
}
