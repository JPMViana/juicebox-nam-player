// SPDX-License-Identifier: GPL-3.0-only
package selfupdate

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestNewer(t *testing.T) {
	for _, c := range []struct {
		a, b string
		want bool
	}{
		{"v0.18.0", "v0.17.0", true}, {"v0.17.0", "v0.17.0", false}, {"v0.17.1", "v0.17.0", true},
		{"v1.0.0", "v0.99.9", true}, {"v0.9.0", "v0.10.0", false}, {"v0.18.0-dev", "v0.17.0", true}, {"", "v0.1.0", false},
	} {
		if got := Newer(c.a, c.b); got != c.want {
			t.Errorf("Newer(%q, %q) = %v", c.a, c.b, got)
		}
	}
}

func TestCheck(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`{"tag_name":"v0.18.0","html_url":"https://github.com/x/y/releases/tag/v0.18.0","body":"notes"}`))
	}))
	defer srv.Close()
	old := API
	API = srv.URL
	defer func() { API = old }()
	in, err := Check(context.Background(), "v0.17.0")
	if err != nil || !in.Available || in.Latest != "v0.18.0" || in.URL == "" {
		t.Fatalf("Check = %+v, %v", in, err)
	}
	if in, _ = Check(context.Background(), "v0.18.0"); in.Available {
		t.Fatalf("same version reported as newer")
	}
}
