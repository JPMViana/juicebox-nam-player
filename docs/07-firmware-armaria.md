# 07 – Firmware Juice-Box (ex-armaria-nam)

Nome: **Juice-Box**, por **João Viana** (até a v0.1.2: armaria-nam). Identidade no protocolo de update: `BlackBox_9xx`
(o modelo continua `BlackBox` para manter compatibilidade com o updater oficial e
com `tools/bb_install.py`).

## v0.8 – v0.10 (2026-10-22/23) — NAM A2-Lite no pedal

- `nam/a2lite.c`: A2-Lite em C, **idêntico ao NeuralAmpModelerCore oficial** (diferença máx. 5e-7,
  `nam/test/a2lite_host.c` contra `render --slim 0.2`; atenção: `--slim 0.5` seleciona o Full).
- Desempenho medido no pedal (240 MHz — o mesmo clock do firmware de fábrica):

  | Versão | Tempo por bloco de 128 amostras (limite 2902 us) |
  |---|---|
  | v0.8 (frame-major, máscara por acesso) | 5792 us |
  | v0.8.2 (tap-major, ring espelhado) | 4736 us |
  | v0.8.3 (kernel na RAM) | 4888 us (sem ganho) |

- Microbenchmarks (`JBP`): float mul+add 4,75 ciclos/MAC no laço real; Q31 com acumulador 64 bits
  5,3–5,6 (mais lento); a FPU **tem** MAC (`r0 += r1 * r2 (f)`, o compilador não emite), mas ele não
  tem pipeline: 3,2 ciclos; mul e add separados entram em pipeline (1,9 ciclos/par).
- **Segundo núcleo (Felucca `fm1_cpu1.h`):** liga e roda tarefas de inteiro e float isoladas (`JB1`), mas:
  - os dois núcleos executando **código da RAM** ao mesmo tempo **travam o chip** (`JB2`, `JB3`, todos os modos);
  - os dois executando da **flash** com laços de inteiros: ok (`JB4` modo 3);
  - v0.10: NAM em pipeline com os dois núcleos na flash (float nos dois) → o núcleo 1 falha, o
    fallback o segura, **e o USB e o DMA de áudio param** (núcleo 0 segue vivo: tela, LEDs, ADC).
    Recuperado religando o pedal e gravando a v0.10.1 no instante em que o USB respondeu.
- **v0.10.1**: segundo núcleo e NAM desligados (passthrough), para seguirmos com segurança.
- **v0.10.2 (atual)** + `JB5` (tudo na flash, laços só em registradores): **núcleo 1 em inteiros +
  núcleo 0 em float também trava** (o watchdog reiniciou). Então não é só "float nos dois": até agora só
  inteiro + inteiro na flash (`JB4` modo 3) funcionou. Modos 2/3 do JB5 não rodaram.
- Faixas internas do A2-Lite (`nam/test/a2lite_ranges.c`, guitarra real, pico de entrada 0,5): pesos ±1,4;
  entrada das camadas até ~14 (cresce nas últimas); ativações até ~21; soma da head ~41. Ponto fixo
  proposto: ativações Q8.23 (±256), pesos Q2.29 (±4), produto em 64 bits deslocado 29 → folga > 6×.
- Próximos passos: (a) testar JB5 modos 2/3 e a carga real com inteiros nos dois núcleos;
  (b) kernel otimizado em assembly num núcleo; (c) se (a) passar, NAM em ponto fixo nos dois núcleos.

## v0.2.x (2026-10-22) — Juice-Box, gravado e rodando (v0.2.2 = `BlackBox_906`)

- Novo nome e logo (jarra de suco), "por João Viana" na tela de abertura; USB:
  produto `Juice-Box`, fabricante `João Viana` (o macOS pode manter o nome antigo
  da porta MIDI em cache: o VID:PID não mudou).
- **Registro de boot na flash** (`0xB9000`, registros de 32 B) + leitura por USB:
  `.venv/bin/python tools/jb_log.py`. Cada boot grava até onde o boot anterior
  chegou (estágio, ms, laços), motivo do reset e os contadores do bootguard.
  - v0.2.1: o setor tinha restos da VM de fábrica; agora só grava em slot 100 %
    vazio e apaga o setor se achar algo que não seja nosso.
  - v0.2.2: contadores fora de faixa são zerados (os carregadores usam a mesma RAM).
- **Em aberto:** ligado só na bateria (sem cabo), o pedal entra no resgate (tela
  preta) mesmo sem a regra removida na v0.1.2. O registro de boot existe para isso.

## v0.1.2 (2026-10-22)

Incidente da v0.1.1: indicador "USB OK" não sumia ao desconectar e, ligado só na
bateria, o pedal ficava de tela preta. Causa (minha): a regra extra de resgate
"barramento resetado mas nunca configurado em 30 s". Sem cabo, as linhas D+/D-
soltas são lidas como reset de barramento → após 30 s o firmware entrava no
carregador de resgate (que não usa a tela). O pedal foi encontrado no carregador
(`ota-?`) e a v0.1.2 foi instalada por ele: **o resgate funcionou como projetado**.

- Regra removida: resgate só por dois resets de watchdog seguidos.
- Indicador USB: `config && !suspended` (barramento parado = cabo fora).
- Linha de diagnóstico por 2 s no boot: `RST` (motivo do reset, registro P33),
  `BOOT` (boots desde a última energização fria), `RESC` (entradas no resgate).
- Identidade `BlackBox_903`.

## v0.1.1 (2026-10-22)

- Tela: área visível nas linhas 0..239 do controlador (a v0.1 usava 40..279); limpa
  toda a RAM do controlador (240×320) antes da imagem, para não sobrar nada da UI de fábrica.
- Identidade `BlackBox_902`; instalada a partir do V20 com `--bump-cfg`.

## v0.1 (2026-10-22)

O que faz:

1. **USB-MIDI + entrada de update primeiro** (dispositivo `armaria-nam`, VID:PID
   `1209:0001` de teste do pid.codes), responde ao handshake como `BlackBox_901`
   e aceita pacotes pelo mesmo protocolo do M-UPGRADE (step 1 do Felucca).
2. **Tela**: init ST7789 copiado do V20, imagem de abertura 240×240 e uma linha de
   status (`armaria-nam v0.1`, `USB OK` quando o computador configura o dispositivo,
   etapas de update, erros).
3. **Resgate**: no primeiro boot grava o carregador oficial da JieLi em
   `0xBA000` (fim da região VM). Dois boots falhos seguidos (watchdog antes de 30 s,
   ou host que reseta o barramento mas nunca configura o dispositivo) entram nesse
   carregador pelo registro de update na RAM; ele aceita o V20 pelo `bb_install.py`.
   Nunca entra no modo UBOOT (que no Mac exigiria hardware extra).

Não faz ainda: áudio, footswitches, botões, knobs.

### Verificado no pedal

- Instalado a partir do app de fábrica (`BlackBox_019` → `BlackBox_901`), 48 + gravação 100 %.
- Enumera como `armaria-nam` / `armaria`; `bb_install.py --info` → `BlackBox_901 [running]`.
- Tela (v0.1): imagem e knob visíveis, mas deslocadas 40 linhas para baixo → corrigido na v0.1.1.
- **Update a partir do armaria-nam → V20: funcionou.** O step 1 do nosso firmware
  aceitou o pacote (75 leituras), gravou o registro na RAM e reiniciou; o SPL subiu o
  carregador oficial (`4D4A:4155`, "Jieli Technology"), que gravou o V20 (100 %) e voltou
  como `BlackBox_020`.
  - Achado: iniciado só com o registro na RAM, o carregador oficial responde ao
    handshake com **identidade vazia** (deve ler a identidade de um registro na flash,
    que não escrevemos de propósito). `bb_install.py` aceita isso como `ota-?`.
- V20 → armaria-nam v0.1.1 de novo: funcionou (`BlackBox_902`).

### Ainda não verificado

- Resgate por boot falho (só ocorre se algo travar).

## Estrutura

```
fw/
  build.py            compila (Docker), monta app.bin, empacota com tools/bbpkg.py
  app.ld              mapa de memória (do Felucca; slot do app = 0xA0160 B)
  src/armaria.c       unidade única: tempo, tela, flash, update, resgate, main
  tools/gen_assets.py splash 240x240 RGB565, fonte 8x16, carregador oficial (build/ apenas)
  assets/             Inter Tight (OFL)
  felucca/            HAL, USB, OTA e crt0 do Felucca (GPL-3.0), commit em FELUCCA_COMMIT;
                      mudanças marcadas "armaria-nam" (constantes do OTA e strings USB sobrescrevíveis)
```

Build e instalação:

```bash
.venv/bin/python fw/build.py                       # -> build/fw/armaria-BlackBox_901.fwsc
.venv/bin/python tools/bb_install.py build/fw/armaria-BlackBox_901.fwsc --force
```

Para instalar a partir do **app de fábrica**, o `cfg` do pacote precisa diferir do
instalado (`--bump-cfg` no build quando necessário). A partir do armaria-nam não há
essa trava.

## Mapa de flash usado

| Região | Uso no armaria-nam |
|---|---|
| `0x00000–0x04000` | cabeçalho do V20 (SPL, isd_config) — nunca alterado |
| `0x04000–0xA9000` | app (o nosso) + cfg_tool + cfg do V20 |
| `0xBA000–0xBF000` | carregador oficial em staging (resgate / update) |
| `0xC2000`, `0xFF000` | BTIF, key_mac — nunca tocados |
