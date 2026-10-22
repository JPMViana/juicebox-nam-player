# blackbox-nam

Custom firmware RE project: M-VAVE ANN BlackBox (JieLi AC791N, pi32v2 @ 360 MHz) as a NAM A2-Lite player.
Docs are in Portuguese (pt-BR); keep them that way.

- Start with `docs/01-hardware-e-firmware.md` (facts) and `docs/03-roadmap.md` (current phase).
- `reference/` holds cloned third-party repos (`scripts/setup_reference.sh`); `AL-255/FM-1-RE` is the same SoC and the main RE prior art.
- Run Python that reads firmware/model files with `python3 -I`; jl-misctools deps live in `.venv`.
- Never write anything to the pedal without a verified full flash backup (`docs/04-seguranca.md`). Never commit vendor binaries.
