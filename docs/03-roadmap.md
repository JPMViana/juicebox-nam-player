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
- [x] Update a partir do armaria-nam: voltar ao V20 e reinstalar (v0.1.1) — funcionou
- [ ] Adicionar codec/áudio (passthrough limpo), depois footswitches/LEDs
- [ ] Tela: identificar o controlador e os pinos (`screen_task` no app de fábrica) e portar o `lcd.c`/`gfx.c` do Felucca
- [ ] Interface própria: nome do modelo NAM, IR on/off, volume/gain, nível de entrada
- [x] A2-Lite nos dois núcleos (front em float no núcleo 0, back em ponto fixo no núcleo 1): estável com VDC14 = 1,50 V,
  2700 us/bloco (v0.13.16, [07](07-firmware-armaria.md))
- [x] NAM ligado no boot + teste com guitarra real (v0.13.17–22): caminho de áudio corrigido (metades perdidas,
  fase da metade do DMA, ADC bloqueante); som "utilizável" com a captura Fender Super Reverb ([07](07-firmware-armaria.md))
- [ ] Refinar o som: taxa de 48 kHz (ou reamostragem), ganho de entrada, ganho/limitador de saída
- [ ] Alternativa de menor esforço, se o port emperrar: patch do `app.bin` de fábrica
  trocando só o motor ANN do bloco AMP

## Fase 5 – Ferramentas para o usuário
- [x] Conversor `.nam` (A2) → pesos A2-Lite (`tools/nam2c.py`, com normalização de loudness), embutido pelo
  `fw/build.py --model` (v0.13.22)
- [x] Cliente de carregamento: upload de capturas `.nam` via USB sem recompilar (`tools/jb_models.py`)
- [x] Escolha de captura no pedal (v0.15–v0.17): bancos FX..CAB com A/B, menu do PARA, troca ao vivo
- [ ] Modo "power amp": saída sem cab/IR, nível de linha ajustável, noise gate antes do NAM

## Fase 6 – Distribuição e código aberto (decisão 2026-11-03)
- [x] Repositório limpo para publicação: objetos do SDK JieLi e WAVs removidos do histórico
  (`git filter-repo`), LICENSE GPL-3.0, README atualizado
- [x] Capturas fora do app (v0.14.2): 8 slots na VM livre (não na `USR`, que é do firmware oficial) +
  protocolo USB, cliente `tools/jb_models.py` ([07](07-firmware-armaria.md))
- [ ] Instalador para usuários não técnicos (Go, Windows e macOS): instalar o Juice-Box, voltar ao
  V20 oficial, carregar capturas `.nam`. O V20 é baixado do fabricante e o pacote é montado na
  máquina do usuário (o `.fwsc` contém partes do fabricante e não é distribuído pronto)
- [ ] VID:PID próprio no pid.codes (hoje `1209:0001`, PID de teste)
- [ ] **Backlog — documentação do usuário (EN + PT-BR)**: como baixar, abrir e usar o instalador no Windows e no
  macOS **sem assinatura paga** (decisão 2026-11-04: sem a conta Apple Developer de US$ 99/ano nem certificado
  Windows). Cobrir: macOS (Gatekeeper: botão direito → Abrir, ou Ajustes → Privacidade e Segurança → "Abrir
  mesmo assim"; `xattr -dr com.apple.quarantine Juice-Box.app` como alternativa), Windows (SmartScreen: "Mais
  informações" → "Executar assim mesmo"; WebView2 já vem no Windows 10/11), cabo USB de dados, fechar o editor
  da M-VAVE, o que fazer se a instalação for interrompida (modo de atualização), voltar ao oficial, checksums
  publicados no release para conferir o download
- [ ] Builds de release por GitHub Actions (macOS universal + Windows), checksums SHA-256 no release
- [ ] Ícone próprio do instalador
- [x] Interface na tela (v0.17), inglês e português
- [ ] Calibrar a bateria, achar o pino de status do carregador, mapear os LEDs (docs/07)

## Fase 7 – IR (depois da primeira versão distribuível)
- [ ] Carregar IRs pelo instalador. Pode funcionar **sem o NAM ao mesmo tempo** (modo só-IR),
  porque o NAM já usa ~2700 de 2902 us por bloco

