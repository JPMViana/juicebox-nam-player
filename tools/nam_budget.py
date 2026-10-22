#!/usr/bin/env python3
"""Estimate the per-sample compute cost of a .nam model and compare it with a
target CPU budget.

For the convolutional (WaveNet/ConvNet) and recurrent (LSTM) architectures NAM
uses, every weight takes part in exactly one multiply-accumulate per output
sample, so the weight count is a good first-order proxy for MACs/sample.
Activations, ring-buffer bookkeeping and memory stalls are NOT included; treat
the numbers as a lower bound and calibrate the `--cycles-per-mac` factor with
real measurements on the target.

Usage:
    python3 -I tools/nam_budget.py model.nam [--mhz 360] [--cores 1]
                                    [--cycles-per-mac 2.0] [--fs 48000]
"""

import argparse
import json
from pathlib import Path


def describe(model, path="root"):
    """Yield (path, architecture, weight_count, sample_rate, summary) for every
    leaf model inside a (possibly nested) .nam document."""
    arch = model.get("architecture")
    cfg = model.get("config", {})
    if arch == "SlimmableContainer":
        for i, sub in enumerate(cfg.get("submodels", [])):
            label = f"{path}/submodel[{i}] max_value={sub.get('max_value')}"
            yield from describe(sub["model"], label)
        return

    weights = model.get("weights", [])
    summary = ""
    if arch == "WaveNet":
        parts = []
        for la in cfg.get("layers", []):
            ks = la.get("kernel_sizes") or [la.get("kernel_size")] * len(la["dilations"])
            parts.append(
                f"ch={la['channels']} layers={len(la['dilations'])} "
                f"k={sorted(set(ks))} max_dil={max(la['dilations'])}"
            )
        summary = " | ".join(parts)
    elif arch == "LSTM":
        summary = f"hidden={cfg.get('hidden_size')} layers={cfg.get('num_layers')}"
    yield path, arch, len(weights), model.get("sample_rate"), summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", type=Path)
    ap.add_argument("--mhz", type=float, default=360.0, help="core clock (BlackBox SYS_CLK = 360 MHz)")
    ap.add_argument("--cores", type=int, default=1, help="cores available for NAM")
    ap.add_argument("--cycles-per-mac", type=float, default=2.0,
                    help="effective cycles per float MAC incl. loads (calibrate on HW!)")
    ap.add_argument("--fs", type=float, default=None, help="override sample rate (Hz)")
    ap.add_argument("--headroom", type=float, default=0.7,
                    help="fraction of CPU allowed for NAM (rest: codec, IR, UI, USB)")
    args = ap.parse_args()

    doc = json.loads(args.model.read_text())
    budget_cycles = args.mhz * 1e6 * args.cores * args.headroom

    print(f"Target: {args.cores} core(s) @ {args.mhz:.0f} MHz, {args.cycles_per_mac} cycles/MAC, "
          f"{args.headroom:.0%} headroom -> {budget_cycles / 1e6:.0f} Mcycles/s for NAM\n")
    for path, arch, nw, fs, summary in describe(doc):
        fs = args.fs or fs or 48000
        macs_s = nw * fs
        cycles_s = macs_s * args.cycles_per_mac
        load = cycles_s / budget_cycles
        verdict = "OK" if load <= 1.0 else "NAO CABE"
        print(f"{path}\n  {arch}: {nw} pesos (~{nw * 4 / 1024:.1f} KiB fp32) {summary}")
        print(f"  ~{macs_s / 1e6:.1f} MMAC/s @ {fs:.0f} Hz -> {load:.0%} do orcamento [{verdict}]\n")


if __name__ == "__main__":
    main()
