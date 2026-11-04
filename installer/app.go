// SPDX-License-Identifier: GPL-3.0-only
package main

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"

	wruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"juicebox/installer/internal/fwbuild"
	"juicebox/installer/internal/jlpkg"
	"juicebox/installer/internal/midiio"
	"juicebox/installer/internal/nam"
	"juicebox/installer/internal/stock"
	"juicebox/installer/internal/store"
	"juicebox/installer/internal/updater"
)

// The Juice-Box release this installer carries (fw/build.py --release, copied in by the build script).
//
//go:embed all:assets/release
var releaseFS embed.FS

// App is what the window calls (bound to window.go.main.App).
type App struct {
	ctx context.Context
	mu  sync.Mutex // one pedal operation at a time
	be  *midiio.RT
	man *fwbuild.Manifest
	app []byte
}

func NewApp() *App { return &App{} }

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	if be, err := midiio.NewRT(); err == nil {
		a.be = be
	}
	if ents, err := releaseFS.ReadDir("assets/release"); err == nil {
		for _, e := range ents {
			if !strings.HasSuffix(e.Name(), ".json") {
				continue
			}
			mb, _ := releaseFS.ReadFile("assets/release/" + e.Name())
			if m, err := fwbuild.ParseManifest(mb); err == nil {
				if b, err := releaseFS.ReadFile("assets/release/" + m.App); err == nil {
					a.man, a.app = m, b
				}
			}
		}
	}
}

func (a *App) shutdown(context.Context) {
	if a.be != nil {
		a.be.Close()
	}
}

// ---------------------------------------------------------------- settings ---

type settings struct {
	Lang string `json:"lang"`
}

func settingsPath() string {
	d, _ := os.UserConfigDir()
	return filepath.Join(d, "Juice-Box", "installer.json")
}

// Lang is the window's language: "en" or "pt", "" = not chosen yet (the UI follows the system).
func (a *App) Lang() string {
	var s settings
	if b, err := os.ReadFile(settingsPath()); err == nil {
		json.Unmarshal(b, &s)
	}
	return s.Lang
}

func (a *App) SetLang(l string) {
	b, _ := json.Marshal(settings{Lang: l})
	os.MkdirAll(filepath.Dir(settingsPath()), 0o755)
	os.WriteFile(settingsPath(), b, 0o644)
}

// ------------------------------------------------------------------ status ---

// Status is what the window shows about the pedal.
type Status struct {
	State    string `json:"state"` // none, stock, juicebox, loader, other, busy, nomidi
	Identity string `json:"identity"`
	Version  int    `json:"version"`
	Release  string `json:"release"` // the Juice-Box version this installer carries ("" = none)
	Product  string `json:"product"` // its identity
}

func (a *App) updater() *updater.Updater { return updater.New(a.be) }

func classify(id updater.Identity) string {
	switch {
	case id.Loader():
		return "loader"
	case updater.ModelOf(id.Text) != updater.Device:
		return "other"
	case id.Version >= 900:
		return "juicebox"
	default:
		return "stock"
	}
}

// Status looks for the pedal (a quick handshake); "busy" while an operation runs.
func (a *App) Status() Status {
	st := Status{}
	if a.man != nil {
		st.Release, st.Product = a.man.Version, a.man.Product
	}
	if a.be == nil {
		st.State = "nomidi"
		return st
	}
	if !a.mu.TryLock() {
		st.State = "busy"
		return st
	}
	defer a.mu.Unlock()
	d := a.updater().Find(nil)
	if d == nil {
		st.State = "none"
		return st
	}
	d.Link.Close()
	st.State, st.Identity, st.Version = classify(d.ID), d.ID.Text, d.ID.Version
	return st
}

func (a *App) progress(step, arg string, n int) {
	wruntime.EventsEmit(a.ctx, "progress", map[string]any{"step": step, "arg": arg, "n": n})
}

// UIError carries a code the window translates.
func uiErr(err error) error {
	var ue *updater.Error
	if errors.As(err, &ue) {
		return fmt.Errorf("E%d: %s", ue.Code, ue.Msg)
	}
	return err
}

func (a *App) install(raw []byte) error {
	product, err := jlpkg.ProductOf(raw)
	if err != nil {
		return err
	}
	up := a.updater()
	d := up.Find(nil)
	if d == nil {
		return fmt.Errorf("E%d: pedal not found", updater.ErrNotFound)
	}
	_, err = up.Install(d, jlpkg.LogicalImage(raw), product, a.progress)
	return uiErr(err)
}

// InstallJuiceBox installs (or updates to) the release this installer carries.
func (a *App) InstallJuiceBox() error {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.man == nil {
		return errors.New("this installer carries no Juice-Box release")
	}
	a.progress("download", "", 0)
	st, err := stock.Load("")
	if err != nil {
		return fmt.Errorf("ESTOCK: %v", err)
	}
	pkg, err := fwbuild.Assemble(st, a.app, a.man, true)
	if err != nil {
		return err
	}
	return a.install(pkg)
}

// RevertStock goes back to the official firmware (Juice-Box's captures and settings are erased first).
func (a *App) RevertStock() error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.progress("download", "", 0)
	st, err := stock.Load("")
	if err != nil {
		return fmt.Errorf("ESTOCK: %v", err)
	}
	if d := a.updater().Find(nil); d != nil {
		if classify(d.ID) == "juicebox" {
			a.progress("wipe", "", 0)
			store.New(d.Link).Wipe() // (an old Juice-Box without the store: nothing to erase)
		}
		d.Link.Close()
	}
	return a.install(st)
}

// --------------------------------------------------------- captures, banks ---

// Captures is the store and the banks, for the window.
type Captures struct {
	Slots []store.Slot        `json:"slots"`
	Banks *store.BankRec      `json:"banks"`
	Names [store.Banks]string `json:"names"`
	Live  bool                `json:"live"`
}

func (a *App) client() (*store.Client, func(), error) {
	d := a.updater().Find(nil)
	if d == nil {
		return nil, nil, fmt.Errorf("E%d: pedal not found", updater.ErrNotFound)
	}
	if classify(d.ID) != "juicebox" {
		d.Link.Close()
		return nil, nil, errors.New("ENOTJB: the pedal is not running Juice-Box")
	}
	return store.New(d.Link), d.Link.Close, nil
}

// GetCaptures reads the slots and the banks.
func (a *App) GetCaptures() (*Captures, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	c, done, err := a.client()
	if err != nil {
		return nil, err
	}
	defer done()
	dir, err := c.Directory()
	if err != nil {
		return nil, err
	}
	b, err := c.Banks()
	if err != nil {
		return nil, err
	}
	return &Captures{Slots: dir.Slots[:], Banks: b, Names: store.BankNames, Live: dir.XIP}, nil
}

// NamInfo describes a .nam file before it is sent.
type NamInfo struct {
	Path  string `json:"path"`
	Title string `json:"title"`
	Rate  int    `json:"rate"`
	Error string `json:"error"`
}

// PickCaptures opens the file dialog and checks each .nam picked.
func (a *App) PickCaptures() ([]NamInfo, error) {
	paths, err := wruntime.OpenMultipleFilesDialog(a.ctx, wruntime.OpenDialogOptions{
		Title:   "NAM captures",
		Filters: []wruntime.FileFilter{{DisplayName: "NAM capture (*.nam)", Pattern: "*.nam"}},
	})
	if err != nil {
		return nil, err
	}
	var out []NamInfo
	for _, p := range paths {
		out = append(out, inspect(p, ""))
	}
	return out, nil
}

func inspect(p, title string) NamInfo {
	ni := NamInfo{Path: p}
	b, err := os.ReadFile(p)
	if err != nil {
		ni.Error = err.Error()
		return ni
	}
	c, err := nam.Read(b, p, title)
	switch {
	case errors.Is(err, nam.ErrNotA2):
		ni.Error = "ENOTA2"
	case errors.Is(err, nam.ErrFixed):
		ni.Error = "EFIXED"
	case err != nil:
		ni.Error = "EBADNAM"
	default:
		ni.Title, ni.Rate = c.Title, c.SampleRate
	}
	return ni
}

// AddItem is one capture to write.
type AddItem struct {
	Path  string `json:"path"`
	Slot  int    `json:"slot"` // 0..7
	Title string `json:"title"`
}

// AddCaptures writes captures to their slots, then the pedal restarts.
func (a *App) AddCaptures(items []AddItem) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	c, done, err := a.client()
	if err != nil {
		return err
	}
	defer done()
	for k, it := range items {
		b, err := os.ReadFile(it.Path)
		if err != nil {
			return err
		}
		cp, err := nam.Read(b, it.Path, it.Title)
		if err != nil {
			return err
		}
		if err := c.Put(it.Slot, cp.Slot(), func(p int) { a.progress("capture", cp.Title, (k*100+p)/len(items)) }); err != nil {
			c.Resume()
			return err
		}
	}
	a.progress("reboot", "", 0)
	return c.Resume()
}

// RemoveCapture empties a slot, then the pedal restarts.
func (a *App) RemoveCapture(slot int) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	c, done, err := a.client()
	if err != nil {
		return err
	}
	defer done()
	if err := c.Erase(slot); err != nil {
		c.Resume()
		return err
	}
	return c.Resume()
}

// SaveBanks writes the banks; the pedal saves them and restarts.
func (a *App) SaveBanks(rec store.BankRec) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	c, done, err := a.client()
	if err != nil {
		return err
	}
	defer done()
	return c.SetBanks(&rec)
}
