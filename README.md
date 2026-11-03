# Juice-Box

**Juice-Box** — firmware NAM para o M-VAVE ANN BlackBox, por **João Viana**.

Firmware alternativo para o pedal **M-VAVE (Sinco) ANN BlackBox**, transformando-o
em um **player de capturas NAM (Neural Amp Modeler) A2-Lite**, pensado para uso
direto no retorno de um power amp analógico (simulação de cabeçote, sem cab/IR
obrigatório).

> **Status (v0.13.22):** o pedal roda uma captura NAM A2-Lite nos dois núcleos, com o
> áudio estável e som utilizável. A captura ainda é embutida na compilação. Próximos passos:
> carregar capturas por USB, interface na tela e um instalador para Windows/macOS.
> Veja [docs/03-roadmap.md](docs/03-roadmap.md).

> ⚠️ **Projeto em desenvolvimento, ainda sem instalador para o usuário final.** Gravar
> firmware pode inutilizar o pedal. Leia [docs/04-seguranca.md](docs/04-seguranca.md) antes.

## O que funciona

- Instalação e volta ao firmware de fábrica (V20) **só por USB**, sem gravador
  (`tools/bb_install.py`, protocolo do M-UPGRADE). Há também um carregador de resgate para
  quando o boot falha.
- NAM A2-Lite em tempo real: a primeira metade da rede roda em float no núcleo 0 e a segunda
  em ponto fixo no núcleo 1 (~2700 µs por bloco de 2902 µs).
- Conversor de capturas `.nam` (A2) → pesos embarcados, com normalização de volume
  (`tools/nam2c.py`).
- Tela própria, mapeamento completo de botões, knobs e footswitches, e registro de boot na flash.

## Por que é viável

| Item | O que sabemos |
|---|---|
| SoC | JieLi **AC791N** (família "WL82"), CPU **pi32v2** (derivada do Blackfin), 2 núcleos |
| Clock | `SYS_CLK = 360 MHz` |
| Flash | 1 MB NOR, região `USR` de 256 KB para dados do usuário |
| Update | USB-MIDI, carregador conferido só por CRC-16 (sem assinatura) |
| NAM A2-Lite | ~1,9 k pesos, ~90 MMAC/s @ 48 kHz → cabe usando os dois núcleos |
| NAM A2-Full | ~12 k pesos, ~583 MMAC/s → **não cabe** neste chip |

Detalhes em [docs/02-viabilidade-nam.md](docs/02-viabilidade-nam.md) e
[docs/07-firmware-armaria.md](docs/07-firmware-armaria.md) (histórico de versões).

## Estrutura

```
docs/        documentação técnica, segurança e roadmap (pt-BR)
fw/          firmware Juice-Box (fw/build.py); fw/felucca/ = HAL/USB/OTA do Felucca
nam/         kernel NAM A2-Lite em C (float, ponto fixo, testes no host)
tools/       bb_install.py (update USB-MIDI), bbpkg.py (.fwsc), nam2c.py, jb_*.py (diagnóstico)
scripts/     setup de referências, desempacotamento do firmware de fábrica, compilador pi32v2
firmware/    imagens de fábrica (NÃO versionadas – ver firmware/README.md)
analysis/    notas e logs das sessões no pedal
reference/   repositórios de terceiros clonados (NÃO versionados)
```

## Compilando (desenvolvedores, macOS)

Requisitos: Docker Desktop, Python 3.

```bash
scripts/setup_reference.sh      # clona FM-1-RE, Felucca, NAM core, etc. em reference/
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git reference/fw-AC79_AIoT_SDK
sh reference/Felucca/tools/get_toolchain.sh "$PWD/.toolchain"   # clang pi32v2 da JieLi
scripts/unpack_stock.sh         # baixa o firmware V20 oficial (necessário para empacotar)
python3 -m venv .venv && .venv/bin/pip install mido python-rtmidi

.venv/bin/python fw/build.py --model minha_captura.nam     # -> build/fw/*.fwsc
.venv/bin/python tools/bb_install.py --info                # identidade do pedal (só leitura)
.venv/bin/python tools/bb_install.py build/fw/<pacote>.fwsc --force
.venv/bin/python tools/bb_install.py firmware/stock/BlackBox_FACTORY_V20.fwsc   # voltar ao oficial
```

O pacote gerado contém partes do firmware oficial (cabeçalho e carregador de update),
por isso **ele é montado localmente** a partir do V20 baixado do fabricante e não é
distribuído pronto.

## Créditos

- [Felucca](https://github.com/hugelton/Felucca) (GPL-3.0): base de HAL, USB e OTA do AC791N.
- [FM-1-RE](https://github.com/AL-255/FM-1-RE): engenharia reversa da mesma plataforma.
- [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore): arquitetura A2 e modelos.
- Fonte Inter Tight (OFL, `fw/assets/OFL.txt`).

## Licença

GPL-3.0 — veja [LICENSE](LICENSE).

Projeto não oficial, sem afiliação com M-VAVE/Sinco/JieLi/TONE3000. Marcas citadas
pertencem aos seus donos. Use por sua conta e risco.
