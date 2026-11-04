// SPDX-License-Identifier: GPL-3.0-only
// jbx: the Juice-Box installer's command line (the GUI uses the same packages).
//
//	jbx ports                                   MIDI ports (sends nothing)
//	jbx info                                    the pedal's identity (read-only)
//	jbx install RELEASE.json [--stock V20.fwsc] install Juice-Box from a release (app.bin next to the manifest)
//	jbx install-fwsc PACKAGE.fwsc               install a ready package (development builds)
//	jbx revert [--stock V20.fwsc]               back to the official firmware (erases Juice-Box's captures first)
//	jbx captures | jbx put FILE.nam SLOT [--title T] | jbx select SLOT | jbx wipe | jbx banks
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"juicebox/installer/internal/fwbuild"
	"juicebox/installer/internal/jlpkg"
	"juicebox/installer/internal/midiio"
	"juicebox/installer/internal/nam"
	"juicebox/installer/internal/stock"
	"juicebox/installer/internal/store"
	"juicebox/installer/internal/updater"
)

func die(f string, a ...any) {
	fmt.Fprintf(os.Stderr, "error: "+f+"\n", a...)
	os.Exit(1)
}

func confirm(yes bool, q string) bool {
	if yes {
		return true
	}
	fmt.Print(q + " [y/N] ")
	s, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	s = strings.ToLower(strings.TrimSpace(s))
	return s == "y" || s == "yes"
}

type progress struct{ line bool }

func (p *progress) step(s, arg string, n int) {
	switch s {
	case "check":
		fmt.Printf("\rchecking the package (%d reads)\033[K", n)
		p.line = true
	case "write":
		fmt.Printf("\rwriting %3d%%\033[K", n)
		p.line = true
	default:
		if p.line {
			fmt.Println()
			p.line = false
		}
		fmt.Println(map[string]string{"start": "starting the update on " + arg, "loader": "switching to update mode",
			"reboot": "restarting", "done": "done: the pedal runs " + arg}[s])
	}
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("usage: jbx ports | info | install RELEASE.json | install-fwsc PKG | revert | captures | put FILE SLOT | select SLOT | wipe | banks")
		os.Exit(2)
	}
	cmd, args := os.Args[1], os.Args[2:]
	fs := flag.NewFlagSet(cmd, flag.ExitOnError)
	port := fs.String("port", "", "MIDI port (part of its name)")
	stockPath := fs.String("stock", "", "the official V20 package (default: download / cache)")
	title := fs.String("title", "", "the capture's name on the pedal")
	yes := fs.Bool("yes", false, "do not ask for confirmation")
	logPath := fs.String("log", "", "write a diagnostics log to this file")
	var pos []string
	for len(args) > 0 { // flags anywhere
		fs.Parse(args)
		args = fs.Args()
		if len(args) > 0 {
			pos = append(pos, args[0])
			args = args[1:]
		}
	}
	be, err := midiio.NewRT()
	if err != nil {
		die("MIDI: %v", err)
	}
	defer be.Close()
	up := updater.New(be)
	up.Port = *port
	if *logPath != "" {
		lf, err := os.Create(*logPath)
		if err != nil {
			die("%v", err)
		}
		defer lf.Close()
		up.Log = func(f string, a ...any) {
			fmt.Fprintf(lf, "%s %s\n", time.Now().Format("15:04:05.000"), fmt.Sprintf(f, a...))
		}
	}
	find := func() *updater.Dev {
		d := up.Find(nil)
		if d == nil {
			die("pedal not found (USB data cable? another app using it?) MIDI inputs: %v", be.Inputs())
		}
		return d
	}
	install := func(raw []byte) {
		product, err := jlpkg.ProductOf(raw)
		if err != nil {
			die("%v", err)
		}
		d := find()
		fmt.Printf("package: %s\ndevice:  %s\n", product, d.ID.Text)
		if !confirm(*yes, "Install? Do not unplug the pedal while writing.") {
			d.Link.Close()
			fmt.Println("cancelled")
			return
		}
		p := &progress{}
		if _, err := up.Install(d, jlpkg.LogicalImage(raw), product, p.step); err != nil {
			if p.line {
				fmt.Println()
			}
			die("%v", err)
		}
	}
	client := func() (*store.Client, func()) {
		d := find()
		return store.New(d.Link), d.Link.Close
	}
	switch cmd {
	case "ports":
		fmt.Println("MIDI inputs: ", be.Inputs())
		fmt.Println("MIDI outputs:", be.Outputs())
	case "info":
		d := find()
		d.Link.Close()
		mode := "running"
		if d.ID.Loader() {
			mode = "update loader (an install was not finished)"
		}
		fmt.Printf("%s  [%s]  port: %s\n", d.ID.Text, mode, d.Link.Name())
	case "install":
		if len(pos) != 1 {
			die("install RELEASE.json")
		}
		mb, err := os.ReadFile(pos[0])
		if err != nil {
			die("%v", err)
		}
		m, err := fwbuild.ParseManifest(mb)
		if err != nil {
			die("%v", err)
		}
		app, err := os.ReadFile(filepath.Join(filepath.Dir(pos[0]), m.App))
		if err != nil {
			die("%v", err)
		}
		st, err := stock.Load(*stockPath)
		if err != nil {
			die("%v", err)
		}
		pkg, err := fwbuild.Assemble(st, app, m, true) // bump-cfg: needed from the stock firmware, harmless otherwise
		if err != nil {
			die("%v", err)
		}
		fmt.Printf("Juice-Box %s\n", m.Version)
		install(pkg)
	case "install-fwsc":
		if len(pos) != 1 {
			die("install-fwsc PACKAGE.fwsc")
		}
		raw, err := os.ReadFile(pos[0])
		if err != nil {
			die("%v", err)
		}
		install(raw)
	case "revert":
		st, err := stock.Load(*stockPath)
		if err != nil {
			die("%v", err)
		}
		d := find()
		if !d.ID.Loader() && d.ID.Version >= 900 { // Juice-Box: leave its flash area erased, as a new pedal's
			fmt.Println("erasing Juice-Box's captures and settings")
			if err := store.New(d.Link).Wipe(); err != nil {
				fmt.Println("  (could not erase:", err, "- continuing)")
			}
		}
		d.Link.Close()
		install(st)
	case "captures":
		c, done := client()
		defer done()
		dir, err := c.Directory()
		if err != nil {
			die("%v", err)
		}
		for i, s := range dir.Slots {
			mark := " "
			if dir.Active == i {
				mark = ">"
			}
			if s.Valid {
				fmt.Printf(" %s %d: %-23s %d Hz, gain %.2f\n", mark, i+1, s.Title, s.Rate, s.Norm)
			} else {
				fmt.Printf(" %s %d: (empty)\n", mark, i+1)
			}
		}
		fmt.Printf("bank %d (%s), live switching: %v\n", dir.Bank+1, store.BankNames[dir.Bank%store.Banks], dir.XIP)
	case "put":
		if len(pos) != 2 {
			die("put FILE.nam SLOT")
		}
		slot, err := strconv.Atoi(pos[1])
		if err != nil || slot < 1 || slot > store.Slots {
			die("slot: 1..%d", store.Slots)
		}
		data, err := os.ReadFile(pos[0])
		if err != nil {
			die("%v", err)
		}
		cap, err := nam.Read(data, pos[0], *title)
		if err != nil {
			die("%s: %v", pos[0], err)
		}
		c, done := client()
		defer done()
		fmt.Printf("%s -> slot %d as %q\n", filepath.Base(pos[0]), slot, cap.Title)
		if err := c.Put(slot-1, cap.Slot(), func(p int) { fmt.Printf("\r  writing %3d%%\033[K", p) }); err != nil {
			fmt.Println()
			die("%v", err)
		}
		fmt.Println()
		c.Resume()
		fmt.Println("written; the pedal restarts")
	case "select":
		slot, err := strconv.Atoi(strings.Join(pos, ""))
		if err != nil || slot < 1 || slot > store.Slots {
			die("select SLOT (1..%d)", store.Slots)
		}
		c, done := client()
		defer done()
		if err := c.Select(slot - 1); err != nil {
			die("%v", err)
		}
		fmt.Println("ok; the pedal restarts")
	case "wipe":
		c, done := client()
		defer done()
		if !confirm(*yes, "Erase every capture and the banks?") {
			return
		}
		if err := c.Wipe(); err != nil {
			die("%v", err)
		}
		c.Resume()
		fmt.Println("erased; the pedal restarts")
	case "banks":
		c, done := client()
		defer done()
		r, err := c.Banks()
		if err != nil {
			die("%v", err)
		}
		dir, _ := c.Directory()
		name := func(s int) string {
			if s < store.Slots && dir != nil && dir.Slots[s].Valid {
				return dir.Slots[s].Title
			}
			return "(empty)"
		}
		for i, b := range r.B {
			mark := " "
			if i == r.Bank {
				mark = ">"
			}
			fmt.Printf(" %s %d %-3s  A: %-23s B: %-23s IN %+.1f OUT %+.1f\n", mark, i+1, store.BankNames[i],
				name(b.Cap[0]), name(b.Cap[1]), float64(b.InHD)/2, float64(b.OutHD)/2)
		}
	default:
		die("unknown command %q", cmd)
	}
}
