// SPDX-License-Identifier: GPL-3.0-only
// Package jlpkg reads and builds M-VAVE ANN BlackBox update packages (.fwsc): a port of tools/bbpkg.py.
//
// Package = 20 interleaved identity marker bytes + a UFW container (header and file list ENC-ciphered
// with 0xFFFF). The UFW we write holds only flash.bin and ota.bin. flash.bin keeps the stock head
// [0, 0x4000) verbatim (SPL, isd_config, top JLFS); the app area is rebuilt from its parts with fresh
// CRCs and SFC-ciphered with the chip key. Formats follow kagaimiq/jl-misctools and Felucca.
package jlpkg

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
)

const (
	blocks  = 20
	blk     = 0x30
	keep    = 0x2F
	noMark  = 0x7D
	appArea = 0x4000
)

// CRC16 is the CRC-16/XMODEM the JieLi formats use.
func CRC16(data []byte, crc uint16) uint16 {
	for _, b := range data {
		crc ^= uint16(b) << 8
		for i := 0; i < 8; i++ {
			if crc&0x8000 != 0 {
				crc = crc<<1 ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

// enc is the JieLi ENC stream cipher, in place (its own inverse).
func enc(buf []byte, key uint16) {
	for i := range buf {
		buf[i] ^= byte(key)
		if key&0x8000 != 0 {
			key = key<<1 ^ 0x1021
		} else {
			key <<= 1
		}
	}
}

// sfc is the SFC cipher: ENC per 32-byte block, the key mixed with the block address.
func sfc(buf []byte, key uint16) {
	for i := 0; i < len(buf); i += 32 {
		end := i + 32
		if end > len(buf) {
			end = len(buf)
		}
		enc(buf[i:end], key^uint16(i>>2))
	}
}

func chipKeyDecode(d []byte) uint16 {
	s := 0
	for _, b := range d[:16] {
		s += int(b)
	}
	s &= 0xFF
	if s >= 0xE0 {
		s = 0xAA
	} else if s <= 0x10 {
		s = 0x55
	}
	var k uint16
	for i := 0; i < 16; i++ {
		if int(d[16+i]^d[15-i]) < s {
			k |= 1 << i
		}
	}
	return k
}

// Entry is a JLFS file entry (32 bytes on flash).
type Entry struct {
	DCRC   uint16
	Offset uint32
	Size   uint32
	Flags  byte
	Resvd  byte
	Index  uint16
	Name   [16]byte
}

func parseEntry(b []byte) (Entry, error) {
	var e Entry
	if len(b) < 32 {
		return e, errors.New("short JLFS entry")
	}
	if CRC16(b[2:32], 0) != binary.LittleEndian.Uint16(b) {
		return e, errors.New("JLFS entry header CRC mismatch")
	}
	e.DCRC = binary.LittleEndian.Uint16(b[2:])
	e.Offset = binary.LittleEndian.Uint32(b[4:])
	e.Size = binary.LittleEndian.Uint32(b[8:])
	e.Flags, e.Resvd = b[12], b[13]
	e.Index = binary.LittleEndian.Uint16(b[14:])
	copy(e.Name[:], b[16:32])
	return e, nil
}

func (e Entry) bytes() []byte {
	b := make([]byte, 32)
	binary.LittleEndian.PutUint16(b[2:], e.DCRC)
	binary.LittleEndian.PutUint32(b[4:], e.Offset)
	binary.LittleEndian.PutUint32(b[8:], e.Size)
	b[12], b[13] = e.Flags, e.Resvd
	binary.LittleEndian.PutUint16(b[14:], e.Index)
	copy(b[16:], e.Name[:])
	binary.LittleEndian.PutUint16(b, CRC16(b[2:], 0))
	return b
}

// FileName is the entry's name up to the first NUL.
func (e Entry) FileName() string {
	if i := bytes.IndexByte(e.Name[:], 0); i >= 0 {
		return string(e.Name[:i])
	}
	return string(e.Name[:])
}

// LogicalImage is what the device reads during an update: the package without the 20 marker bytes.
func LogicalImage(raw []byte) []byte {
	out := make([]byte, 0, len(raw))
	for i := 0; i < blocks; i++ {
		out = append(out, raw[i*blk:i*blk+keep]...)
	}
	return append(out, raw[blocks*blk:]...)
}

// ProductOf is the package identity ("BlackBox_020"): one marker byte after each of the first 20 blocks.
func ProductOf(raw []byte) (string, error) {
	if len(raw) < blocks*blk {
		return "", errors.New("not a JieLi package (too short)")
	}
	var s []byte
	for i := 0; i < blocks; i++ {
		if m := raw[i*blk+keep]; m != noMark {
			s = append(s, m-byte(i)-1)
		}
	}
	return string(s), nil
}

// UFWFile is one file of the container.
type UFWFile struct {
	Type uint16
	Name string
	Data []byte
}

func ufwFiles(logical []byte) (map[string]UFWFile, error) {
	if len(logical) < 0x40 {
		return nil, errors.New("not a UFW package")
	}
	hdr := append([]byte(nil), logical[:0x40]...)
	enc(hdr, 0xFFFF)
	if CRC16(hdr[2:0x40], 0) != binary.LittleEndian.Uint16(hdr) || string(hdr[16:22]) != "AC791N" {
		return nil, errors.New("not an AC791N UFW package")
	}
	n := int(binary.LittleEndian.Uint16(hdr[8:]))
	files := map[string]UFWFile{}
	for i := 0; i < n; i++ {
		o := 0x40 + i*0x50
		if o+0x50 > len(logical) {
			return nil, errors.New("truncated UFW file list")
		}
		e := append([]byte(nil), logical[o:o+0x50]...)
		enc(e, 0xFFFF)
		typ := binary.LittleEndian.Uint16(e)
		off := binary.LittleEndian.Uint32(e[8:])
		ln := binary.LittleEndian.Uint32(e[12:])
		if uint64(off)+uint64(ln) > uint64(len(logical)) {
			return nil, errors.New("UFW file outside the package")
		}
		name := e[0x40:]
		if j := bytes.IndexByte(name, 0); j >= 0 {
			name = name[:j]
		}
		files[string(name)] = UFWFile{typ, string(name), logical[off : off+ln]}
	}
	return files, nil
}

// UFWPack builds a .fwsc from files (Felucca fm1pkg_make.ufw layout) with the identity product.
func UFWPack(files []UFWFile, product string) ([]byte, error) {
	if len(product) > blocks {
		return nil, fmt.Errorf("product %q: at most %d characters", product, blocks)
	}
	for i := 0; i < len(product); i++ {
		if product[i]+byte(i)+1 == noMark {
			return nil, fmt.Errorf("product %q: character %d encodes to the unused marker", product, i)
		}
	}
	type ent struct {
		e   []byte
		off int
		d   []byte
	}
	off, ents := 0x400, []ent{}
	for i, f := range files {
		off = (off + 0xFF) &^ 0xFF
		e := make([]byte, 0x50)
		binary.LittleEndian.PutUint16(e[0:], f.Type)
		binary.LittleEndian.PutUint16(e[2:], uint16(i))
		binary.LittleEndian.PutUint16(e[4:], CRC16(f.Data, 0))
		binary.LittleEndian.PutUint32(e[8:], uint32(off))
		binary.LittleEndian.PutUint32(e[12:], uint32(len(f.Data)))
		binary.LittleEndian.PutUint32(e[16:], uint32((len(f.Data)+31)&^31))
		copy(e[0x40:], f.Name)
		ents = append(ents, ent{e, off, f.Data})
		off += len(f.Data)
	}
	total := off
	hdr := make([]byte, 0x40)
	binary.LittleEndian.PutUint32(hdr[4:], uint32(total))
	binary.LittleEndian.PutUint16(hdr[8:], uint16(len(files)))
	binary.LittleEndian.PutUint16(hdr[10:], 0x0004)
	binary.LittleEndian.PutUint16(hdr[12:], 0x0200)
	copy(hdr[16:], "AC791N")
	var lst []byte
	for _, e := range ents {
		x := append([]byte(nil), e.e...)
		enc(x, 0xFFFF)
		lst = append(lst, x...)
	}
	binary.LittleEndian.PutUint16(hdr[2:], CRC16(lst, 0))
	binary.LittleEndian.PutUint16(hdr[0:], CRC16(hdr[2:], 0))
	enc(hdr, 0xFFFF)
	logical := bytes.Repeat([]byte{0xFF}, total)
	copy(logical, hdr)
	copy(logical[0x40:], lst)
	for _, e := range ents {
		copy(logical[e.off:], e.d)
	}
	raw := make([]byte, 0, total+blocks)
	for i := 0; i < blocks; i++ {
		m := byte(noMark)
		if i < len(product) {
			m = product[i] + byte(i) + 1
		}
		raw = append(raw, logical[i*keep:(i+1)*keep]...)
		raw = append(raw, m)
	}
	return append(raw, logical[blocks*keep:]...), nil
}

// Stock is an unpacked stock package: what a new package is built from.
type Stock struct {
	Product   string
	ChipKey   uint16
	FlashLen  int
	Head      Entry   // the app area head
	Entries   []Entry // the app area's entries
	CfgOffset int
	CfgRaw    []byte
	FlashHead []byte            // [0, 0x4000): SPL, isd_config, top JLFS
	Data      map[string][]byte // the data files of the app area (app.bin, cfg_tool.bin)
	OTA       []byte            // the package's update loader (ota.bin)
	Flash     []byte            // the stock flash.bin, as is
}

// Unpack splits a stock .fwsc into its parts.
func Unpack(raw []byte) (*Stock, error) {
	product, err := ProductOf(raw)
	if err != nil {
		return nil, err
	}
	files, err := ufwFiles(LogicalImage(raw))
	if err != nil {
		return nil, err
	}
	fl, ok1 := files["flash.bin"]
	ota, ok2 := files["ota.bin"]
	if !ok1 || !ok2 || len(fl.Data) < appArea+0x200 {
		return nil, errors.New("no flash.bin / ota.bin in the package")
	}
	flash := fl.Data
	head0 := append([]byte(nil), flash[:0xA0]...) // flash header + top entries: ENC'd per 32-byte slot
	for o := 0; o < len(head0); o += 32 {
		enc(head0[o:o+32], 0xFFFF)
	}
	var key uint16
	found := false
	for i := 0; i < 4; i++ {
		e, err := parseEntry(head0[0x20+0x20*i:])
		if err != nil {
			return nil, err
		}
		if e.FileName() == "isd_config.ini" {
			key = chipKeyDecode(flash[e.Offset : e.Offset+32])
			found = true
		}
	}
	if !found {
		return nil, errors.New("no isd_config.ini in the flash head")
	}
	area := append([]byte(nil), flash[appArea:]...)
	sfc(area, key)
	head, err := parseEntry(area)
	if err != nil {
		return nil, err
	}
	s := &Stock{Product: product, ChipKey: key, FlashLen: len(flash), Head: head, Data: map[string][]byte{},
		FlashHead: append([]byte(nil), flash[:appArea]...), OTA: ota.Data, Flash: flash}
	for o := 0x20; o < 0x120; o += 0x20 {
		if bytes.Equal(area[o:o+32], bytes.Repeat([]byte{0xFF}, 32)) {
			break
		}
		e, err := parseEntry(area[o:])
		if err != nil {
			return nil, err
		}
		s.Entries = append(s.Entries, e)
	}
	s.CfgOffset = int(head.Size)
	cfg, err := parseEntry(area[s.CfgOffset:])
	if err != nil {
		return nil, err
	}
	s.CfgRaw = append([]byte(nil), area[s.CfgOffset:s.CfgOffset+int(cfg.Size)]...)
	for _, e := range s.Entries {
		if e.Flags == 0x82 {
			s.Data[e.FileName()] = append([]byte(nil), area[e.Offset:e.Offset+e.Size]...)
		}
	}
	return s, nil
}

// BuildFlash rebuilds flash.bin with app (nil: the stock app.bin) in the app slot. bumpCfg flips a padding byte
// of the nested eq_cfg_hw.bin entry name, so the cfg header differs from the installed one: the stock firmware
// refuses a package whose cfg header matches its own.
func (s *Stock) BuildFlash(app []byte, bumpCfg bool) ([]byte, error) {
	area := bytes.Repeat([]byte{0xFF}, s.FlashLen-appArea)
	for idx, e := range s.Entries {
		if e.Flags == 0x82 {
			data := s.Data[e.FileName()]
			if e.FileName() == "app.bin" && app != nil {
				data = app
			}
			if uint32(len(data)) != e.Size {
				return nil, fmt.Errorf("%s: %d B, the stock slot is %d B (sizes must match)", e.FileName(), len(data), e.Size)
			}
			copy(area[e.Offset:], data)
			e.DCRC = CRC16(data, 0)
		}
		copy(area[0x20+0x20*idx:], e.bytes())
	}
	h := s.Head
	h.DCRC = CRC16(area[0x20:s.CfgOffset], 0)
	copy(area[0:], h.bytes())
	cfg := append([]byte(nil), s.CfgRaw...)
	if bumpCfg {
		inner, err := parseEntry(cfg[0x20:])
		if err != nil {
			return nil, err
		}
		inner.Name[15] ^= 0x01
		copy(cfg[0x20:], inner.bytes())
		outer, err := parseEntry(cfg)
		if err != nil {
			return nil, err
		}
		outer.DCRC = CRC16(cfg[0x20:], 0)
		copy(cfg, outer.bytes())
	}
	copy(area[s.CfgOffset:], cfg)
	sfc(area[:s.CfgOffset+len(cfg)], s.ChipKey) // the rest stays plain 0xFF, as in stock
	return append(append([]byte(nil), s.FlashHead...), area...), nil
}

// Package builds an installable .fwsc: the stock flash with app in the app slot, the stock update loader.
func (s *Stock) Package(app []byte, product string, bumpCfg bool) ([]byte, error) {
	flash, err := s.BuildFlash(app, bumpCfg)
	if err != nil {
		return nil, err
	}
	return UFWPack([]UFWFile{{0, "flash.bin", flash}, {100, "ota.bin", s.OTA}}, product)
}
