// SPDX-License-Identifier: GPL-3.0-only
// Juice-Box installer: install Juice-Box on an M-VAVE ANN BlackBox, manage its captures and banks, go back to the
// official firmware. Wails app (the window is frontend/dist); cmd/jbx is the same over the command line.
package main

import (
	"embed"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
)

//go:embed all:frontend/dist
var assets embed.FS

func main() {
	app := NewApp()
	err := wails.Run(&options.App{
		Title:            "Juice-Box",
		Width:            980,
		Height:           680,
		MinWidth:         820,
		MinHeight:        600,
		AssetServer:      &assetserver.Options{Assets: assets},
		BackgroundColour: &options.RGBA{R: 14, G: 14, B: 16, A: 1},
		OnStartup:        app.startup,
		OnShutdown:       app.shutdown,
		Bind:             []interface{}{app},
	})
	if err != nil {
		println("Error:", err.Error())
	}
}
