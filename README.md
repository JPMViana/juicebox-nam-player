# blackbox-nam

Firmware alternativo para o pedal **M-VAVE (Sinco) ANN BlackBox**, transformando-o
em um **player de capturas NAM (Neural Amp Modeler) A2** para uso direto no
power amp analógico (simulação de cabeçote, sem cab/IR obrigatório).

> Status: **Fase 1 concluída** — gravação por USB no macOS comprovada, inclusive de app
> modificado. Próximo: mapear o hardware no app de fábrica e o primeiro firmware próprio. Veja [docs/04-seguranca.md](docs/04-seguranca.md).

## Por que é viável (e onde não é)

| Item | O que sabemos |
|---|---|
| SoC | JieLi **AC791N** (família "WL82"), CPU **pi32v2** (derivada do Blackfin) |
| Clock | `SYS_CLK = 360 MHz` (lido do `isd_config.ini` do firmware V20) |
| Flash | 1 MB NOR (`0xFF000`), região `USR` de 256 KB para dados do usuário |
| Firmware | `.fwsc` = container UFW JieLi, criptografado com chip-key `0x980F` — **já conseguimos desempacotar** |
| NAM A2-Lite | ~1,9 k pesos, **~90 MMAC/s @ 48 kHz** → cabe: pi32v2 tem MAC nativa 64 bits (~50–80 % de 1 núcleo) |
| NAM A2-Full | ~12 k pesos, **~583 MMAC/s @ 48 kHz** → **não cabe** neste chip |

Detalhes em [docs/02-viabilidade-nam.md](docs/02-viabilidade-nam.md).

## Estrutura

```
docs/        documentação técnica e roadmap
tools/       nam_budget.py (custo de .nam), bb_install.py (update por USB-MIDI), bbpkg.py (empacotador .fwsc)
nam/         código NAM embarcado (bench/ = kernels de teste pi32v2)
scripts/     setup de referências e desempacotamento reproduzível
firmware/    imagens de fábrica (NÃO versionadas – ver firmware/README.md)
analysis/    saídas de análise (desempacotamento, strings, Ghidra)
reference/   repositórios de terceiros clonados (NÃO versionados)
```

## Começando

Requisitos (macOS): Docker Desktop, Python 3, `brew install libraqm` (só para compilar o Felucca).

```bash
scripts/setup_reference.sh      # clona FM-1-RE, Felucca, jl-misctools, NAM core, etc.
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git reference/fw-AC79_AIoT_SDK
sh reference/Felucca/tools/get_toolchain.sh "$PWD/.toolchain"   # clang pi32v2 da JieLi
scripts/unpack_stock.sh         # baixa e desempacota o firmware V20 de fábrica
.venv/bin/pip install mido python-rtmidi

python3 -I tools/nam_budget.py reference/NeuralAmpModelerCore/example_models/A2.nam
scripts/pi32cc.sh clang -O2 -mcpu=r3 -mfprev1 -S nam/bench/conv_kernel3.c -o /dev/stdout
.venv/bin/python tools/bb_install.py --list     # portas MIDI (não envia nada)
.venv/bin/python tools/bb_install.py --info     # identidade do pedal (só leitura)
```

## Roadmap resumido

0. **Reconhecimento** – firmware, hardware, protocolo de update *(em andamento)*
1. **Caminho USB comprovado** – toolchain, cliente de update no macOS, ida e volta com o V20 *(sem gravadores)*
2. **Desmontagem do firmware** – Ghidra pi32v2, mapear pinos, codec e o motor "ANN" atual
3. **Prova de conceito** – kernel A2-Lite em pi32v2 medido em hardware real
4. **Firmware próprio** – port do [Felucca](https://github.com/hugelton/Felucca) (FM-1) para o BlackBox + A2-Lite no 2º núcleo
5. **Ferramentas** – conversor `.nam` → formato embarcado, carregamento via USB

Roadmap completo: [docs/03-roadmap.md](docs/03-roadmap.md).

## Aviso

Projeto não oficial, sem afiliação com M-VAVE/Sinco/JieLi/TONE3000. Gravar
firmware pode inutilizar o pedal. Use por sua conta e risco.
