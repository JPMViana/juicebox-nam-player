# 03 – Roadmap

Estratégia: **só USB, sem gravadores** — ver [04-seguranca.md](04-seguranca.md).
Base de código: [Felucca](https://github.com/hugelton/Felucca) (FM-1, GPL-3.0),
portado para o hardware do BlackBox.

## Fase 0 – Reconhecimento *(em andamento)*
- [x] Obter firmware de fábrica V20 e desempacotar (`scripts/unpack_stock.sh`)
- [x] Identificar SoC, clock, mapa de flash, chip-key
- [x] Estimar custo do NAM A2 Lite/Full (`tools/nam_budget.py`)
- [ ] Fotos da placa (opcional, só chave de fenda): codec, pinos de footswitch/LED/display
- [ ] Ler o `docs/` do FM-1-RE sobre protocolo OTA e boot (mesma plataforma)
- [ ] Conferir que o `ota.bin` do V20 é idêntico ao do FM-1 (mesmo carregador)

## Fase 1 – Caminho USB comprovado no BlackBox
- [x] Toolchain JieLi (clang pi32v2) + SDK AC79 via Docker no macOS — Felucca compila inteiro (`.fwsc` gerado)
- [x] Protocolo de update do BlackBox = o do FM-1 (mesmo comando `F0 22 24 35 7F F7`, mesmo worker do M-UPGRADE)
- [x] Cliente de update para macOS: `tools/bb_install.py` (derivado do Felucca, GPL-3.0) — testado sem o pedal
- [x] **Com o pedal:** `bb_install.py --list` e `--info` → porta `USB Composite Device`, identidade `BlackBox_009`
- [x] Gravação do **V20 de fábrica** pelo nosso cliente no macOS (2026-10-22): `BlackBox_009` → `BlackBox_020`, 47 leituras de verificação + 1335 de gravação, pedal voltou sozinho. Log em `analysis/logs/`
- [x] Empacotador próprio `tools/bbpkg.py`: reconstrói o `flash.bin` do V20 **byte a byte idêntico** (`selftest`)
- [x] **App modificado aceito e rodando** (2026-10-22): `BlackBox_020` → `BlackBox_019` (4 bytes do app alterados + `--bump-cfg`), gravado com `bb_install.py --force`; o pedal passou a responder `BlackBox_019`. Sem assinatura: o pedal executa código nosso
- [ ] Mapear no `app.bin` V20 (Ghidra): pinos de USB, codec, footswitches, LEDs/display, bateria

## Fase 2 – Desmontagem do firmware
- [ ] Ghidra + SLEIGH pi32v2 (scripts do FM-1-RE) sobre `app.bin` V20
- [ ] Mapear: init do codec, DMA de áudio, cadeia de efeitos, motor "ANN" atual
- [ ] Descobrir formato do modelo ANN do fabricante (arquitetura, tamanho, onde fica na `USR`)
- [ ] Verificar se o segundo núcleo é inicializado

## Fase 3 – Prova de conceito NAM
- [x] Benchmark estático (assembly) fp32 vs Q31 → MAC nativa de 64 bits, ~3,3 instr./MAC ([05](05-benchmark-estatico.md))
- [ ] Benchmark no pedal → ciclos/MAC reais (timer de 24 MHz)
- [ ] Port mínimo do A2-Lite (C puro, sem Eigen) validado bit a bit contra o NAM Core no PC
- [ ] Teste de laço: entrada → A2-Lite → saída no pedal

## Escopo do firmware próprio (decisão 2026-10-22)

Firmware enxuto, focado em resposta rápida. **Fica:** player NAM A2 (Lite), IR
loader opcional, USB (update + carga de modelos/IRs), footswitches, tela com
interface própria. **Sai:** Bluetooth, USB Audio, efeitos (mod/delay/reverb/comp),
motor ANN do fabricante, editor oficial. Cadeia de áudio:
`entrada → gate → NAM A2 → [IR] → volume → saída` (modo power amp = IR desligado).

## Fase 4 – Firmware próprio (port do Felucca)
- [x] **armaria-nam v0.1** (2026-10-22): USB + update + resgate + tela com splash, gravado e rodando ([07](07-firmware-armaria.md))
- [ ] Testar update a partir do armaria-nam (voltar ao V20 e reinstalar)
- [ ] Adicionar codec/áudio (passthrough limpo), depois footswitches/LEDs
- [ ] Tela: identificar o controlador e os pinos (`screen_task` no app de fábrica) e portar o `lcd.c`/`gfx.c` do Felucca
- [ ] Interface própria: nome do modelo NAM, IR on/off, volume/gain, nível de entrada
- [ ] Adicionar A2-Lite no 2º núcleo (o Felucca já usa o 2º núcleo do AC791N: `FELUCCA_CPU2`)
- [ ] Alternativa de menor esforço, se o port emperrar: patch do `app.bin` de fábrica
  trocando só o motor ANN do bloco AMP

## Fase 5 – Ferramentas para o usuário
- [ ] Conversor `.nam` (A2) → blob binário compacto (pesos + config)
- [ ] Upload de modelos via USB (reaproveitar o protocolo do editor ou USB-MSC)
- [ ] Modo "power amp": saída sem cab/IR, nível de linha ajustável, noise gate antes do NAM
