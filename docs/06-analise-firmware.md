# 06 – Análise do firmware de fábrica (V20)

## Método

1. `bbpkg.py unpack` → `analysis/v20_pkg/app.bin` (copiado para `analysis/disasm/app_v20.bin`).
2. O binário é embrulhado num objeto (`.incbin`), linkado em `0x02000000`
   (`analysis/disasm/link.ld`) e desmontado com o `objdump` da própria JieLi
   (`scripts/pi32cc.sh common/bin/objdump -d`) → `analysis/disasm/app_v20.dis`
   (~245 mil linhas). Mesmo método do FM-1-RE; o objdump da JieLi é a referência
   (o SLEIGH pi32v2 do Ghidra tem instruções não resolvidas).
3. `scripts/xr` (`tools/xref.py`) acha quem carrega um endereço, lista funções
   e mostra strings referenciadas.
4. Nomes de periféricos: o mapa oficial do SDK (`WL82.h`), extraído para
   `analysis/notes/wl82_sfr_map.txt` (`lsfr_base = 0x10000`,
   `hsfr_base = 0x40000`, `map_adr(g, a) = (64 g + a) * 4`).

Strings ficam num pool mesclado (o compilador carrega base + deslocamento),
então procurar o endereço exato de uma string raramente funciona. O caminho
produtivo é partir dos **registradores de periféricos**.

## Mapa de memória do app

| Região | Endereço | Tamanho |
|---|---|---|
| Código/rodata (XIP) | `0x02000000` – `0x020A0160` | 640 KB |
| `.data` (LMA na flash) | `0x02091720` → RAM `0x01C00000` | `0xEB40` (59 KB) |
| `.bss` | `0x01C0EB40` | `0x1A5E0` (108 KB) |
| Pilhas | `sp = 0x01C1CC3C`, `ssp = 0x01C1DC3C` | — |

## Periféricos acessados (contagem de referências)

| Periférico | Base | Refs | Uso provável |
|---|---|---|---|
| `JL_ANA` | `0x11900` | 34 | configuração analógica (ADC/DAC de áudio, LDOs) |
| `JL_SFC` | `0x40200` | 27 | flash |
| `JL_AUDIO` | `0x12F00` | 17 | **codec interno (ADC/DAC)** |
| `JL_POWER` | `0x10000` | 16 | clock/energia |
| `JL_P33` | `0x13E00` | 14 | domínio de baixo consumo (bateria/carga?) |
| `JL_WL` | `0x14000` | 11 | Wi-Fi/BT (vai sair) |
| `JL_SPI1` | `0x11D00` | 5 | **tela** |
| `JL_ALNK0/1` | `0x12E00` / `0x15100` | 3 / 1 | I²S (codec externo? a confirmar) |
| `JL_SRC`, `JL_EQ` | `0x14300` / `0x1E000` | 3 / 1 | conversor de taxa e EQ em hardware |
| `JL_PSRAM` | `0x40500` | 2 | PSRAM (presença a confirmar) |
| `JL_ADC` | `0x13100` | 4 | SARADC (bateria, potenciômetros?) |

## Tela — **resolvido**

Controlador **ST7789** (comandos MIPI-DCS), interface SPI de 4 fios, só escrita.

| Sinal | Pino | Fonte |
|---|---|---|
| SCK | **PB6** | SPI1 "port A" do SDK (`IOMAP_CON1.bit4 = 0`) |
| MOSI | **PB7** | SPI1 "port A" |
| D/C | **PB5** | escrito direto em `PORTB_OUT` (`0x50040`, bit 5) |
| CS | **PB1** | `PORTB_OUT` bit 1 |
| RESET ou backlight | PC7 (gpio 39)? | levado a 0 antes do init — a confirmar |

- `JL_SPI1->CON = 0x4020`, `BAUD = 1` (SCK = LSB/2), envio de dados por DMA
  (`ADR`/`CNT`), espera pelo bit 15 do `CON`.
- **Área visível 240×240** na RAM de um controlador 240×320: `CASET 0..239`,
  `RASET 40..279`. A rotina de limpar tela (`0x2028ED0`) varre os 320×240 inteiros
  e depois manda `DISPON (0x29)`.
- Cor **RGB565** (`COLMOD 0x55`), `MADCTL 0x00`, inversão ligada.

Tabela de init em `0x0205CD40` (23 entradas de 18 bytes `[cmd][len][16 dados]`,
lida por `0x0202A39E`; as entradas 1 e 3 são pausas, com o tempo em ms no byte 1):

```
01 SWRESET          ; 120 ms
11 SLPOUT           ; 120 ms
2A CASET  00 00 00 EF
2B RASET  00 28 01 17
B2 PORCTRL 0C 0C 0C 00 33
20 INVOFF
B7 56   BB 18   C0 2C   C2 01   C3 1F   C4 20   C6 0F
D0 A6 A1
E0 D0 0D 14 0B 0B 07 3A 44 50 08 13 13 2D 32
E1 D0 0D 14 0B 0B 07 3A 44 50 08 13 13 2D 32
36 00   3A 55   E7 00   51 FF
21 INVON
```

Funções: init `0x0202A39E` (GPIO + SPI1 + tabela), limpar/ligar `0x02028ED0`,
janela + blit `0x0201E292`.

**Para o nosso firmware:** o `lcd.c` do Felucca (ST7789 240×240 na SPI1) serve
quase direto. Trocar pinos (port A: PB6/PB7, D/C PB5, CS PB1), o offset de linha
(+40) e a tabela de init acima.

## Próximos alvos

- [ ] Áudio: entrada (ADC interno via `JL_AUDIO`/`JL_ANA` ou I²S via `ALNK`?), DAC de saída, taxa, DMA
- [ ] Footswitches e encoders/botões (GPIO ou SARADC)
- [ ] PC7 e backlight; LEDs
- [ ] Bateria/carga (`JL_P33`, SARADC)
