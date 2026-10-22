# 02 – Viabilidade do NAM A2 no AC791N

## O que é o A2

O NAM Architecture 2 (TONE3000 + Steven Atkinson, 2026) é distribuído como um
`SlimmableContainer`: um único `.nam` com submodelos escolhidos conforme a CPU
disponível. No exemplo oficial (`NeuralAmpModelerCore/example_models/A2.nam`):

| Submodelo | `max_value` | Canais | Camadas | Pesos | MMAC/s @ 48 kHz |
|---|---|---|---|---|---|
| **A2-Lite** | 0.5 | 3 | 23 (kernels 6/15, dil. até 239) | 1 871 | **~90** |
| **A2-Full** | 1.0 | 8 | 23 | 12 146 | **~583** |
| A1-Standard (referência) | – | 16/8 | 20 | 13 802 | ~663 |

Ativação: `LeakyReLU(0.01)` — barata (sem `tanh`), ótima para embarcado.
Referência da TONE3000: A2-Lite usa ~50 % de um Cortex-M7 a 600 MHz
(≈300 Mciclos/s → ~3,3 ciclos/MAC incluindo overhead).

Gerado com `tools/nam_budget.py`.

## Orçamento no BlackBox

CPU: pi32v2 @ 360 MHz, possivelmente 2 núcleos (stock usa 1).

| Cenário | Ciclos/MAC | Núcleos | Carga A2-Lite | Carga A2-Full |
|---|---|---|---|---|
| Pessimista (igual ao M7 da TONE3000) | 3,3 | 1 | ~82 % de 1 núcleo, **sem folga** | impossível |
| Realista otimizado | 2,0 | 1 | ~50 % | ~320 % |
| Otimista (asm/SIMD, 2 núcleos) | 1,0 | 2 | ~12 % | ~81 % |

## Conclusões

1. **A2-Lite é o alvo realista.** Cabe, mas exige kernel otimizado à mão
   (pi32v2 não tem Eigen/NEON; precisamos de loops de convolução dilatada
   especializados, buffers em SRAM interna, possivelmente ponto fixo Q15/Q31).
2. **A2-Full só seria possível** se (a) o segundo núcleo estiver utilizável **e**
   (b) atingirmos ≈1 ciclo/MAC. Improvável; tratar como meta estendida.
3. Rodar a 44,1 kHz reduz a carga em ~8 %, mas o modelo foi treinado a 48 kHz
   (mudança de timbre sutil) — avaliar trocar o clock do codec para 48 kHz.
4. Memória não é problema: A2-Lite tem ~7 KB de pesos fp32; o maior buffer
   de histórico (dil. 239 × kernel 6 × 3 canais) é de poucos KB.

**Atualização (benchmark estático, [05](05-benchmark-estatico.md)):** a FPU do
pi32v2 não tem FMA (~4,2 instr./MAC em float), mas há **MAC inteira nativa com
acumulador de 64 bits** (~3,3 instr./MAC hoje, meta ~2). Alvo: kernel em Q31.

**Atualização:** o firmware aberto Felucca já roda código no **segundo núcleo**
do AC791N (`FELUCCA_CPU2`), então o cenário de 2 núcleos é realista: um núcleo
inteiro dedicado ao A2-Lite.

## Plano B (se o pi32v2 não entregar)

Troca do "cérebro": manter gabinete, footswitches, jacks e fonte do BlackBox e
colocar uma placa Cortex-M7 600 MHz (ex.: Teensy 4.1 / i.MX RT1062, ou
Daisy Seed / STM32H750 a 480 MHz) com codec próprio. É exatamente a classe de
chip em que a TONE3000 mediu o A2-Lite, então o risco de desempenho cai a zero,
mas vira um projeto de hardware.

## Próximo experimento decisivo

Medir **ciclos/MAC reais** no pi32v2: compilar um kernel de convolução dilatada
com o toolchain do SDK AC79 e rodar no pedal (ou em uma devboard AC791N barata),
cronometrando com o timer do sistema. Isso define se seguimos com o Plano A.
