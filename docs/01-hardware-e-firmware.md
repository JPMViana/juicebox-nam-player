# 01 – Hardware e firmware de fábrica

Fonte primária: `BlackBox_FACTORY_V20.fwsc` (700 436 bytes,
SHA-256 `c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08`),
extraído do updater oficial pelo projeto
[entitymar/annblackbox-custom-firmware-installer](https://github.com/entitymar/annblackbox-custom-firmware-installer).
Desempacotado com `kagaimiq/jl-misctools` (`scripts/unpack_stock.sh`).

## Fatos confirmados (a partir do binário)

### Container UFW

```
chip name: "AC791N"     PID: "AC791N_STORY"     Flash size: 0x0FF000
chip-key (isd_config.ini): 0x980F          entry point: 0x02000120
```

| Offset flash | Entrada | Tamanho | Observação |
|---|---|---|---|
| `0x000A0` | `uboot.boot` (SPL) | 14 384 | blob do fabricante |
| `0x038D0` | `isd_config.ini` | 699 | config de boot (clock, SDRAM, UART) |
| `0x04120` | `app.bin` | 655 712 | aplicação pi32v2 (XIP em `0x02000000`) |
| `0xA4280` | `cfg_tool.bin` | 383 | parâmetros (contém `JL_AC79XX`, `JL_AC79XX_BLE`) |
| `0xA9000` | `VM` | 100 KB | armazenamento chave-valor (configurações) |
| `0xC2000` | `BTIF` | 4 KB | dados Bluetooth |
| `0xC3000` | `USR` | 256 KB | **dados do usuário – provável local dos AMP/CAB "PC" carregados** |
| `0xFF000` | `key_mac` | 4 KB | chave/MAC por dispositivo — **nunca sobrescrever** |

A chip-key `0x980F` é a mesma do M-VAVE FM-1 (mesma plataforma), então todo o
trabalho do [AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE) é diretamente
reaproveitável (Ghidra pi32v2, protocolo OTA, mapa de boot).

### `isd_config.ini` (valores decodificados, little-endian u32)

| Chave | Valor | Significado |
|---|---|---|
| `OSC_FREQ` | 24 000 000 | cristal 24 MHz |
| `SYS_CLK` | **360 000 000** | CPU a 360 MHz (o FM-1 roda a 240 MHz) |
| `HSB_DIV` / `LSB_DIV` | 1 / 2 | barramentos |
| `UTBD` | 1 000 000 | **UART de debug a 1 Mbaud** |
| `RESET` | `PB01_00_0` | pino de reset longo |
| `ENABLE_SDRAM` | 0 | **SDRAM externa aparentemente desabilitada** (só SRAM interna ~578 KB?) — confirmar |

### Strings relevantes em `app.bin`

- `BlackBox_020` – identificador de produto/versão
- `AMP %d`, `PC AMP`, `CAB %d`, `PC CAB` – slots de fábrica e slots carregados pelo PC
- `mnt/sdfile/app/usr` – sistema de arquivos sobre a região `USR`
- `SYSTEM-*modified #define CPU_CORE_NUM 1*` – a lib de sistema foi compilada
  com **1 núcleo**; o AC791N é dual-core → **o segundo núcleo pode estar ocioso**
- `sdram`, `sdram_powerup_ok` – código de SDRAM presente (pode ser só do SDK)
- Atualização: `usb_update2.bin`, `ble_app_ota.bin`, `LOADER.BIN`, etc.

## USB (observado no pedal, 2026-10-22)

| Item | Valor |
|---|---|
| VID:PID | `4c4a:c755` (19530:51029) — **o mesmo PID do FM-1 em modo normal** |
| Fabricante / produto | `Jieli Technology` / `USB Composite Device` |
| Porta MIDI (macOS) | `USB Composite Device` |
| Identidade (handshake) | `BlackBox_009` de fábrica nesta unidade; `BlackBox_020` após gravar o V20 |

A numeração da identidade (`_009`, `_020`) é interna e não bate necessariamente
com o nome comercial do pacote. Não temos a imagem `_009`: não há volta para ela.

## Especificações declaradas pelo fabricante

44,1 kHz / 24 bit · 20 AMPs "ANN" + slots de terceiros · IR 1024 amostras mono ·
USB Audio (UAC) · Bluetooth (áudio) · bateria interna · USB-C 5 V.

**Nota:** NAM é treinado a 48 kHz. Ou reconfiguramos o codec para 48 kHz, ou
fazemos SRC 44,1↔48 kHz (custo extra de CPU), ou treinamos/convertemos a 44,1 kHz.

## Perguntas em aberto (precisam de hardware na mão)

- [ ] Fotos da placa: marcação do SoC, chip de flash (SOIC-8?), codec externo ou ADC/DAC interno, test points UART
- [ ] SDRAM existe? (`ENABLE_SDRAM=0` sugere que não)
- [ ] Topologia analógica de entrada/saída (nível, impedância) — importante para o power amp
- [ ] Formato do arquivo "ANN" de terceiros que o editor oficial envia (capturar via USB)
- [ ] O segundo núcleo do AC791N está realmente disponível/habilitado?
- [ ] O pi32v2 tem FPU com MAC de ponto flutuante de 1 ciclo? SIMD 16 bit (herança Blackfin)?
