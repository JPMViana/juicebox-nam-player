# 05 – Benchmark estático do kernel A2-Lite no pi32v2

Data: 2026-10-22. Compilador: JieLi clang 4.0.1 (`pi32v2`, toolchain
`jieli-linux-toolchains-20260730.1`) via `scripts/pi32cc.sh` (Docker).
Fontes em `nam/bench/`. "Estático" = contagem de instruções no assembly gerado;
ciclos reais só com o pedal (timer de 24 MHz, `TIMER4`).

## Flags

As flags do SDK oficial são obrigatórias: **`-mcpu=r3 -mfprev1`**.
Sem elas o float vira chamada de software (`__mulsf3`/`__addsf3`), dezenas de
ciclos por operação.

## Resultados (camada A2-Lite: 3 canais, kernel 6, dilatada)

| Variante | Arquivo | MACs/iter. | Instr./iter. | **Instr./MAC** | Observação |
|---|---|---|---|---|---|
| float, ordem ingênua | `conv_kernel.c` | 18 | ~87 | ~4,8 | muitos *spills* para a pilha |
| float, saídas juntas | `conv_kernel2.c` | 9 | ~38 | **~4,2** | `fmul` + `fadd` separados; **não há FMA** (`fmaf` vira chamada de libm) |
| **Q31 + acumulador 64 bits** | `conv_kernel3.c` | 9 | ~30 | **~3,3** | MAC nativo: `r5_r4 += r14 * r7 (s)` — 1 instrução por MAC |

- `__builtin_pi32v2_smuladd64` existe mas o backend falha com `-mcpu=r3`
  ("Cannot select"); o padrão C `acc += (int64_t)a * b` gera a mesma instrução.
- `-mllvm -pi32v2-enable-simd=true` não mudou nada nestes kernels.
- Ainda há ~4 *reloads* da pilha por iteração no Q31; com layout de pesos
  ajustado / desenrolamento manual a meta é **~2 instr./MAC**.

## O que isso significa para o A2-Lite (~90 MMAC/s @ 48 kHz)

Assumindo ~1 instrução/ciclo (a confirmar no hardware):

| Kernel | Mciclos/s | 1 núcleo @ 360 MHz | 2 núcleos |
|---|---|---|---|
| float atual (4,2) | ~378 | 105 % ❌ | ~53 % cada |
| Q31 atual (3,3) | ~297 | 83 % ⚠️ | ~41 % cada |
| Q31 otimizado (meta 2,0) | ~180 | **50 % ✅** | ~25 % cada |

**Decisão provisória:** kernel NAM em **ponto fixo Q31 com acumulador de 64 bits**
(precisão de sobra para áudio: ~186 dB de faixa dinâmica no acumulador), código
quente em RAM (`.ram_text`, não XIP da flash) e uso do 2º núcleo como reserva.

## Incógnitas que só o hardware responde

- Latência real da MAC de 64 bits e das cargas da SRAM (há *stalls*?)
- Custo do cache/XIP se o código não couber em `.ram_text`
- Clock: o BlackBox usa 360 MHz; o FM-1/Felucca roda a 240 MHz (o SPL configura)
