# 07 – Firmware Juice-Box (ex-armaria-nam)

Nome: **Juice-Box**, por **João Viana** (até a v0.1.2: armaria-nam). Identidade no protocolo de update: `BlackBox_9xx`
(o modelo continua `BlackBox` para manter compatibilidade com o updater oficial e
com `tools/bb_install.py`).

## v0.15 – v0.17 (2026-11-03/04) — interface no visor, bancos, bilíngue

| Versão | Pacote | O que mudou | Verificado |
|---|---|---|---|
| v0.15.0 | `_978` | **Troca de captura ao vivo**: a região de capturas vira uma janela XIP sem criptografia (como o `fl_plain_window_init` do Felucca, a partir de `0xA9000`); no boot o firmware compara leituras XIP × driver (`ms_xip_ok`). A troca lê os pesos direto da flash com o núcleo 1 rodando (ocioso): ~0,2 s de silêncio. Fontes suavizadas 4 bpp com acentos (`fonts_aa.h`). Sem captura embutida | `xip_ok = 1`; trocas pelos footswitches funcionaram |
| v0.15.0 | `_978` | Experimento `z`: religar o núcleo 1 em tempo de execução. Variantes 1 e 2 (limites de escrita desligados): ele não responde em 20 ms; variante 0 (como no boot): não responde **e** derruba o pedal (`DBG_MSG 0x800`) | salvar continua sendo um reinício (~1,5 s) |
| v0.16.0 | `_979` | **Bancos**: FX, AMP, MOD, DLY, REV, CAB = 6 bancos de 2 capturas (A/B) com IN/OUT por banco (meio dB). Footswitch direito = A↔B; esquerdo = efeito liga/desliga (toque) e mudo (segurar 0,6 s; uma pisada desmuta). PARA: menu do banco; segurar = salvar. Registro de 64 B (`"JBK1"`) em `0xBF000`. Comandos `t`/`u` (tabela), `p` (P33/portas), `l` (LEDs) | controles ok pelo usuário |
| v0.17.0 | `_980` | Interface **em inglês (padrão) e português** (menu Language/Idioma, salvo no registro). Bateria pelo **PC9** (= metade da tensão, referência 3,3 V assumida). Correção: antes do 1º ADC a escada do PC10 lia 0 = "CAB pressionado" → banco 6 em todo boot | boot no banco 1; bateria mostra 97 % |

### Achados de hardware
- O carregador **interno** do AC791N está desligado (`P3_CHG_CON0..3 = 0`; leitura P33 conferida com `0x06 = 0x0D`, `0x09 = 0x1D`):
  o BlackBox carrega a bateria por um **chip carregador separado** (tem LED de carga próprio). Ele funciona com o nosso firmware.
- Os canais internos de PMU do SARADC (VBG, VBAT/4, LDO5V/4) leem 0 / valor fixo nesta placa: inúteis aqui.
- PC9 = 645 (no USB, carregado há horas) → 4161 mV pela conta atual; o pedal mostra 97 %.

### Pendências
- **Calibrar a bateria**: no USB por muito tempo o usuário espera 100 % (mostra 97 %). Medir PC9 só na bateria, carregando e cheia,
  de preferência com multímetro, e ajustar `BAT_REF_MV` e a curva `MV[]`.
- **Pino de status do carregador** (para o ícone "carregando"): HOME mostra as entradas `A B C H`; comparar bateria × carregador de tomada × cheio.
  Hoje o "+" = host USB conectado.
- **Mapa dos LEDs**: `BANK_LED[]` é chute; mapear com `l` (bit por bit, a tela mostra `LED 0xNN`).
- Religar o núcleo 1 sem reiniciar (permitiria salvar sem os ~1,5 s).

## v0.14.0 – v0.14.2 (2026-11-03) — capturas pelo USB, sem recompilar

O pedal guarda **8 capturas** na flash e toca a escolhida no boot. A captura embutida no build
(`fw/build.py --model`) continua como reserva (slot "0"). Cliente: `tools/jb_models.py`. Ele é a referência
para o instalador.

```bash
.venv/bin/python -I tools/jb_models.py list
.venv/bin/python -I tools/jb_models.py put "minha.nam" 1 --select     # grava no slot 1 e toca
.venv/bin/python -I tools/jb_models.py select 2                       # 0 = captura interna
.venv/bin/python -I tools/jb_models.py wipe                           # apaga tudo (antes de voltar ao V20)
```

| Versão | Pacote | O que mudou | Medido |
|---|---|---|---|
| v0.14.0 | `_975` | Armazenamento de capturas + comandos SysEx `d e w c s x r` (minúsculos). `nam2c.py --slot` gera a imagem do slot e recusa pesos fora do ponto fixo do núcleo 1 | gravar um slot: **0,5 s**; ao retomar, o **núcleo 1 religado faz o pedal cair** (erro de limite de escrita dentro de `fm1_cpu1_start`, `DBG_MSG 0x800`) |
| v0.14.1 | `_976` | Retomar = **reiniciar** o pedal (o boot carrega a captura salva). Antes do reinício proposital o `bootguard` é zerado, senão duas trocas rápidas levariam ao resgate | `select` → reinício → captura certa; capturas mantidas após atualizar o firmware |
| v0.14.2 | `_977` | Correção: o limite de 20 s sem comandos comparava `now` (do topo do laço, mais antigo) com `ms_last_ms` sem sinal → reinício logo após o 1º comando (laço de reinícios durante o teste). Cliente reenvia um `w` sem resposta (até 3×; regravar os mesmos bytes não altera a flash) | 4 trocas seguidas < 10 s após o boot, 3 gravações seguidas, gravação abandonada → reinício em 20 s: tudo ok, sem registros de falha |

### Mapa e formato

| Região | Uso |
|---|---|
| `0xA9000–0xB9000` | 8 slots × 8 KB (início livre da VM) |
| `0xBF000` | configuração: slot ativo (registros de 16 B, só acrescenta; apaga o setor quando enche) |
| `0xC3000–` (`USR`) | **não usado**: é o sistema de arquivos do firmware oficial (AMP/CAB do usuário), preservado para a volta ao V20 |

Slot = cabeçalho de 64 B (`"JBM1"`, versão 1, 1871 pesos, CRC-32 dos pesos, taxa, ganho de loudness, título de 24 B,
CRC-32 do cabeçalho) + pesos float32 LE na ordem do `.nam`. O host grava os pesos e **o cabeçalho por último**; `c`
confere os dois CRCs. Um slot incompleto aparece vazio.

### Protocolo (SysEx `F0 7D 'J' 'B' cmd … F7`, respostas `… cmd slot rc`, rc 0 = ok)

| Cmd | Argumentos | Efeito |
|---|---|---|
| `d` | — | por slot `d slot válido pack7(cabeçalho)`, depois `d 7F ativo versão_protocolo` |
| `e` | slot | pausa o NAM e apaga o slot |
| `w` | slot, deslocamento (3 × 7 bits), pack7(≤ 256 B) | grava; só em slot apagado nesta sessão |
| `c` | slot | confere cabeçalho e CRC → slot válido |
| `s` | slot (7F = interna) | salva como captura do boot e **reinicia** |
| `x` | — | apaga todos os slots e a configuração |
| `r` | — | reinicia (volta a tocar) |

Códigos de erro: 1 pedido inválido, 2 leitura, 3 CRC, 4 modelo, 5 apagar, 6 gravar, 7 cabeçalho.
Pausado e sem comandos por 20 s, o pedal reinicia sozinho. A troca de captura leva ~5 s (o boot ainda tem 3,5 s de
tela de abertura/diagnóstico: encurtar na fase de interface).

### Pendências
- Religar o núcleo 1 sem reiniciar (permitiria trocar de captura no pedal sem os ~5 s).
- Escolha de captura pelos botões do pedal (fase de interface).
- O V20 de fábrica ainda não foi testado com capturas gravadas na VM: o instalador deve rodar `x` antes de voltar
  ao oficial (deixa a região apagada, como uma VM nova).

## v0.13.18 – v0.13.22 (2026-10-23) — som utilizável: caminho de áudio corrigido, captura do usuário embutida

Primeiro teste com guitarra (v0.13.17, `BlackBox_965`): volume baixo, som picotado e chiado digital. Eram três
defeitos do caminho de áudio, mais um problema de montagem do teste. O NAM em si estava certo.

| Versão | Pacote | O que mudou | Medido |
|---|---|---|---|
| v0.13.18 | `_966`–`_969` | (1) pendências do ALNK limpas **ao detectar** a metade: limpas no fim do bloco, apagavam a da próxima, e 14 % das metades se perdiam. (2) O núcleo 1 começa o back do bloco n no fim da chamada, e o núcleo 0 não espera mais por ele. (3) ADC dos pots **sem bloqueio**, um canal por varredura de 5 ms: as 3 leituras bloqueantes seguravam o áudio por ~670 us a cada 15 ms. (4) `text_at`/`status` chamam `poll()` por linha. | 295 → **344,7 blocos/s**; saídas atrasadas ~40/s → **0**; NAM 2710 → **2455 us/bloco**; maior trecho sem poll 670 → 146 us |
| v0.13.19 | `_970` | `JBW` + `tools/jb_capture.py` + `nam/test/a2l_cap_host.c`: 48 blocos de entrada crua e saída do núcleo 1, comparados no Mac com o mesmo modelo a partir do mesmo estado (reset no 1º bloco forte) | 125 saltos em 6144 amostras de entrada, **todos nas primeiras ~10 amostras de cada bloco** |
| v0.13.20 | `_971` | **Metade do DMA ressincronizada**: o `CON0` bit 15 não alterna, então a metade pronta era só contada em software. Uma metade perdida uma vez (boot, reset do modelo, e antes 1 em cada 7) deixava `au_h` na metade em andamento para sempre: estalo a cada 2,9 ms, com ou sem NAM. Agora a última palavra de cada metade lida recebe `0x5A5A5A5A` (as palavras RX têm o byte alto 0): a metade em andamento ainda tem a marca, e `au_h` é corrigido. | 0 saltos; saída do pedal **igual ao host** (diferença 1e-7); 1 ressincronização, no boot |
| v0.13.21 | `_972` | `JBE` / `tools/jb_quiet.py`: desliga LEDs, PWM ou visor para caçar ruído | o chiado de ciclo de 3–4 s que sobrava (mesmo com o MASTER em 0) é o **terra do cabo USB** (pedal no Mac + saída na interface do mesmo Mac): sem o cabo some, e encostando o cabo na carcaça volta |
| v0.13.22 | `_973`–`_974` | `fw/build.py --model X.nam` (A2 / A2-Lite); `nam2c` gera `_NORM` (loudness → -18 dB, como o plugin) e `_TITLE` (visor). Saída = normalização × 2,5 (+8 dB) com **limitador suave** acima de 0,7 da escala (CLIP só se passaria do topo). `_974` = **FNDR BFSR VB Edge WRM2 CAB FREE** (Fender Super Reverb 1965, amp+cab, Tone3000) | ponto fixo com essa captura: SNR 125–130 dB contra o float; NAM 2460 us/bloco |

Avaliação do usuário com a `_974`: "ainda não está excelente, mas já está utilizável".

Para testar sem o chiado de terra: grave e meça pelo USB, depois tire o cabo e alimente o pedal por bateria ou carregador
para ouvir pela interface do mesmo Mac.

Diagnóstico disponível pelo USB:
- `tools/jb_nam_on.py --no-enable --watch N`: blocos/s, ressincronizações, saídas atrasadas e maior trecho sem poll por seção do loop (`JBS`, `JBG`);
- `tools/jb_capture.py DIR`: captura e comparação com o host;
- `tools/jb_quiet.py M`: desliga LEDs, PWM ou visor.

### Pendências / próximos passos
- Refinar o som: o pedal roda a ~44,1 kHz e as capturas são de 48 kHz (trocar a taxa ou reamostrar); ajuste de
  ganho de entrada (a guitarra chega a -1,4 dBFS); revisar o ganho e o limitador de saída de ouvido; IR opcional.
- Usabilidade: cliente para carregar `.nam` pelo USB sem recompilar (hoje: `fw/build.py --model`), escolha de
  captura no pedal, interface no visor (nome, nível de entrada e de saída).
- Ainda pendentes: o profiler para após 12 amostras; o log de boot guarda 1 registro.

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

## v0.13.15 – v0.13.16 (2026-10-23) — **resolvido: VDC14 em 1,50 V; NAM estável nos dois núcleos**

- **Causa**: a 240 MHz, com os dois núcleos ativos e escrita densa na RAM, o núcleo 0 lia **bits trocados** (ex.:
  `[0x01C70504] = 0xad8ffe61`, esperado `0x8d8ffe61`, só o bit 29). Num endereço de retorno ou numa instrução, isso
  desviava o fluxo para dados (`au_rx`) → exceção → reset. Não era conflito de banco nem bug de lógica.
- **VDC14** (`P3_ANA_CON6` bits 2–0, 1,25 V + 0,05 V × nível) é a entrada do LDO do SYSVDD. Padrão 1,40 V (nível 3).
  Subir só o SYSVDD não resolvia; subir o VDC14 resolve:

  | VDC14 | SYSVDD | JBY modo 9 (núcleo 1 escreve, núcleo 0 lê) |
  |---|---|---|
  | 1,40 V | 1,32 V | reset em ~12 ms |
  | 1,40 V | 1,38 V | 15 539 bits errados em 46 M leituras (núcleo 0) |
  | **1,50 V** | 1,38 V | **0 erros** (5 s ×2, 20 s, 20 s com float = modo 25) |
  | **1,50 V** | 1,32 V | **0 erros** (10 s) |

- **NAM em dois núcleos com VDC14 = 1,50 V e SYSVDD = 1,32 V: 120 s sem falha, 2700 us/bloco (máx. 2706; limite
  2902), núcleo 1 nunca atrasado.**
- **v0.13.16 (`BlackBox_964`)**: o firmware põe o VDC14 em 1,50 V no boot (`VDC14_LEVEL`, logo após `usb_start`).
  O NAM continua desligado no boot; liga por USB (`tools/jb_nam_on.py`).
- Ferramentas novas: `tools/jb_volt.py --vdc14 N`, `tools/jb_reboot.py` (JBB), `tools/jb_hsb.py` (JBH: o divisor do HSB
  divide também a CPU, 240 → 120 MHz; a 120 MHz tudo passava, foi o que apontou para margem elétrica), JBY com o
  primeiro erro de cada núcleo.
- **v0.13.17 (`BlackBox_965`)**: `nam_start()` (boot e JBN) liga o NAM **no boot** e atualiza o visor. Antes, o boot
  deixava o 2º núcleo desligado de propósito e o visor mostrava "NAM: 2o NUCLEO FALHOU" mesmo sem falha.
- Modelo embutido: `reference/NeuralAmpModelerCore/example_models/A2.nam` (exemplo do NAM Core, sem nome de
  equipamento; metadados: 48 kHz, loudness -26,4 dB, input 14,76 dBu). O pedal roda a ~44,1 kHz, então o modelo sai
  levemente deslocado em frequência. Pendente: carregar `.nam` próprio pelo USB e decidir a taxa.
- Pendências: (1) teste longo com áudio real e a saída -12 dB (`NAM_OUT_TRIM`); (2) ~~ligar o NAM no boot~~ (v0.13.17); (3) corrigir o
  profiler (para após 12 amostras) e o log de boot (só guarda 1 registro); (4) limpar os testes JB2–JB9/JBY quando não
  forem mais úteis.

## v0.13.12 – v0.13.13 (2026-10-23, manhã) — não é conflito de banco

- **O JBY da 958/959 derrubava o próprio pedal** cerca de 12 ms após começar: o uptime (`crumb.ms`, 0x01C7C0B8)
  voltava a zero e nunca vinha resposta. Os quadros USB de 14–17 bytes chegam certos (`sx_frame` lido com o JBM).
- **v0.13.12 (`BlackBox_960`)**: JBY com byte de modo (1 = sem profiler, 2 = núcleo 0 só atende o USB,
  4 = núcleo 1 parado) e `csync` a cada volta do laço do núcleo 0. Com isso, **todos os modos passam**, inclusive o 0.
- **Bancos (JBY, núcleo 1 lendo 0x01C60000, 20 s cada)**: núcleo 0 a 64 KB, 32 KB, 4 KB, **mesmo endereço**,
  pool (0x01C30000) e `.bss` (0x01C0BA2C) → **0 erros em ~360 M + 377 M leituras, sem reboot**, mesma vazão em
  todos. Leituras densas dos dois núcleos não conflitam, em lugar nenhum.
- JB6 na 960 (núcleo 0 com o front em float): região 1 (`.bss`, travava na 958) e região 2 → 0 erros em 20 s.
- **O NAM em dois núcleos (`jb_nam_on.py`) continua caindo em 5–10 s** (reboot; o pedal volta em passthrough).
- **O profiler para depois de 12 amostras** (TIMER5 deixa de disparar ~12 ms após `prof_start`), então o PC
  registrado no boot não é o do instante da queda. Precisa ser corrigido antes de servir para achar a trava.
- `tools/jb_log.py`: faltava a pausa de 0,4 s depois de abrir a porta (a primeira SysEx se perdia → "no answer").
- **Hipótese nova**: o que o pipeline tem e os testes não têm é **escrita** do núcleo 1 na RAM com o núcleo 0
  em float (v0.12.0 já pegou o núcleo 1 escrevendo fora dos buffers; JB9 + front em float também falha).
  **v0.13.13 (`BlackBox_961`, compilada, ainda não gravada)**: JBY modo 8 = núcleo 1 escreve e relê o padrão
  (`mw_job`), modo 16 = núcleo 0 roda o front em float. Plano: `jb_bank.py X Y 20 M` com M = 8, 16, 24
  (8 + 16), nas mesmas regiões.
- v0.13.13 também reduz a saída do NAM em **-12 dB** (`NAM_OUT_TRIM` 0,25, com o MASTER no máximo); é um primeiro
  valor, a ser ajustado ouvindo.

### Resultados da v0.13.13 (`BlackBox_961`) — a falha é escrita do núcleo 1 + acesso denso do núcleo 0

| JBY (modo) | Núcleo 1 | Núcleo 0 | Resultado |
|---|---|---|---|
| 1 / 0 | lê | lê | ok (20 s, 0 erros) |
| 17 | lê | front em float | ok |
| 11 | **escreve** + relê | só USB | ok (10,6 mil voltas de 4 KB/s, 0 erros) |
| 9 | **escreve** + relê | lê | **reset** em ~12 ms (volta 129) ou ~3 s |
| 25 | **escreve** + relê | front em float | **reset** |

- Independe da região: núcleo 1 escrevendo no pool (0x01C30000), na RAM alta (0x01C60000) ou na RAMTEXT
  (0x01C02000) → reset. Depois de vários resets seguidos o pedal entrou no **carregador de resgate** (`ota-?`), como
  projetado.
- O reset é uma **exceção do núcleo 0** (`fm1_crash`: vec 1, `p3_rst` = 0, ou seja, sem watchdog nem queda de tensão):
  PC = 0x01C09818, **dentro do buffer de áudio `au_rx`**; ETM só com saltos dentro dele; RETS = 0x02001742 (`poll`,
  logo após `call led_write`). O núcleo 0 passou a executar dados: uma instrução ou um endereço de retorno lido errado,
  o mesmo tipo de defeito do bit 21 no RETS do núcleo 1 (v0.12.3).
- Endereços úteis (v0.13.13): uptime `crumb.ms` 0x01C7C0B8, `fm1_crash` 0x01C7C000 (noinit), `boot_rec` muda a cada build
  (ver `build/fw/juicebox.syms`).
- O cache de dados do SDK só cobre endereços ≥ 0x04000000 (SDRAM), então coerência de cache não explica a falha.
- Suspeita seguinte: **clock do barramento**. `SYS_DIV` (0x10008) = 0x00010200, ou seja, divisor do HSB = 0 (barramento no
  clock do sistema). O SDK tem `system_clock_hsb_lsb_div()` (`hsb_clk = sys_pll / div`, 1..4).
  **v0.13.14 (`BlackBox_962`)**: comando `JBH d` (`tools/jb_hsb.py D`) muda esse divisor em tempo de execução, a partir da
  RAM, e mede o clock da CPU. Plano: `jb_hsb.py 1`, depois JBY modo 9/25 e o NAM.

### Resultados da v0.13.14 (`BlackBox_962`) — é margem de tempo a 240 MHz

- `JBH 1` (divisor do HSB = 1) divide **também a CPU**: `clk_loop` de 8333 us → 16667 us (240 → 120 MHz).
- **A 120 MHz tudo passa**: JBY modo 9 (20 s, duas combinações de regiões) e **modo 25 (núcleo 1 escrevendo + front em
  float, o caso do NAM)** → 0 erros, sem reset. A falha é de margem de tempo do chip com os dois núcleos a 240 MHz.
- Tensões (P33): `P3_ANA_CON6` = 0x0b → **VDC14 = 1,40 V** (entrada do LDO do SYSVDD; o SDK usa 1,40 V e SYSVDD 1,26 V
  por padrão). Com SYSVDD em 1,38 V a folga do LDO é de só 20 mV.
- 240 MHz + SYSVDD 1,38 V, modo 9 (5 s): **sem reset**, mas 4,3 M leituras erradas no núcleo 1 e 19 M (56 %) no núcleo 0,
  e o **núcleo 1 sofreu uma exceção** (`EMU_MSG` = 1, parado no tratador). Os testes seguintes deram 0 voltas no núcleo 1
  (`job 9, done 5`), então os números precisam ser repetidos depois de reiniciar o pedal.
- O SDK (`cpu/wl82/debug.c`, `exception_auto_fix_config`) tem uma "correção automática de exceção" que sobe VDDIOM e
  SYSVDD e, por último, baixa o clock da SDRAM: a JieLi trata exceções desse tipo como problema de tensão/clock.
- Próximo firmware: comando de reboot; JBV também com VDC14 (`P3_ANA_CON6` bits 0–2, 1,25–1,60 V); JBY devolvendo o
  primeiro endereço e valor errados dos dois núcleos (para ver se são bits de endereço trocados, como o bit 21 do RETS).

## v0.13.x (2026-10-23) — NAM em dois núcleos: ponto fixo no núcleo 1, travas por conflito de RAM

**Estado ao parar (v0.13.11, `BlackBox_958`)**: o pedal liga com o NAM desligado (passthrough, seguro).
O NAM em dois núcleos é ligado só por USB (`tools/jb_nam_on.py`) e ainda trava em ~6 s.

### Feito
- `nam/a2lq.c`: segunda metade do A2-Lite (camadas 13..22 + head) em **ponto fixo** (pesos Q2.29,
  ativações Q8.23, acumulador 64 bits com a instrução `r1_r0 += r2 * r3 (s)` via asm inline); sem
  nenhuma instrução de float (o build verifica). `nam/a2lx.c`: conversões e prewarm (núcleo 0).
  Host (`nam/test/a2lx_host.c`): **SNR 121 dB** e diferença máxima 2,8e-7 contra o modelo em float.
- Pipeline: núcleo 0 roda o front em float e converte para Q8.23; o núcleo 1 roda o back em ponto fixo.
  - v0.13.0: 60 s sem falha, mas 2940 us/bloco (> 2902);
  - v0.13.1 (MAC explícito): **2674 us/bloco** (espera pelo núcleo 1 ~200 us), porém trava em ~6–15 s.
- Tudo que o núcleo 1 usa fica em `.c1_bss` (0x01C50000–0x01C5DBD0).
- Ferramentas de diagnóstico:
  - **profiler** por TIMER5 a 1 kHz: PC dos dois núcleos gravado em `.noinit` e registrado no boot como
    "PROFILER" em `tools/jb_log.py`;
  - as operações de flash rodam com as interrupções desligadas;
  - a espera do núcleo 1 virou "calma" (sem martelar a caixa de correio).
- Testes novos, todos pelo USB:
  - JB9 (`tools/jb_qback.py`)
  - JBX (`tools/jb_arith.py`)
  - JB6 com duração (`tools/jb_misread.py SEG REGIÕES`)
  - JBY (`tools/jb_bank.py X Y SEG`)
  - JBV agora também define a tensão usada nos testes (`nam_vlevel`).

### O que os testes mostram

| Núcleo 1 | Núcleo 0 | Resultado |
|---|---|---|
| back em ponto fixo (JB9) | só serviço / inteiros | ok, 20 s |
| back em ponto fixo (JB9) | front em float | núcleo 0 lê endereço inválido |
| laço denso de mul 32 bits sobre tabela em 0x01C5C9E4 (JBX) | front em float | trava (mesmo a 1,38 V) |
| leituras densas em **0x01C60000** (JB6, 30 s) | front em float | **0 erros em 538 M leituras** |
| leituras densas em **0x01C0BA2C** (.bss) (JB6) | front em float | **trava** |
| recursão (pilha) em 0x01C50000 (JB8, 120 s) | front em float | ok |

- A tensão não resolve (1,38 V também trava). O SDK inicia o núcleo 1 igual ao Felucca (`EnableOtherCpu` e
  `cpu1_start` desmontados de `system.a` / `cpu.a`); os divisores de clock são `0x10008 = 0x00010200`.
- **Hipótese atual**: conflito de **banco de SRAM**. Quando os dois núcleos acessam intensamente o mesmo
  banco, há leituras erradas e travas; o float só parecia o culpado por ser o código com mais acesso à memória.

### Próximo passo (não executado)
1. Medir o tamanho dos bancos com o JBY, um par de regiões por chamada (o laço do zsh não separou os
   argumentos):
   - `tools/jb_bank.py 0x01C60000 0x01C70000 20` (64 KB)
   - `tools/jb_bank.py 0x01C60000 0x01C68000 20` (32 KB)
   - `tools/jb_bank.py 0x01C60000 0x01C61000 20` (4 KB)
2. Se confirmado, separar a memória por banco:
   - **só do núcleo 1**: `namq`, os anéis das camadas 13..22, a pilha dele, os buffers Q;
   - **só do núcleo 0**: `nam`, os anéis 0..12, `z`, as pilhas, os buffers de áudio e USB.
3. Ganhar margem de tempo no front em float (núcleo 0 em ~2,4 ms).
4. Pendências: reduzir o ganho de saída; carregar `.nam` pelo USB; taxa de amostragem 48 kHz × 44,1 kHz;
   IR; UI.

## v0.11.x – v0.12.x (2026-10-23) — segundo núcleo: o que falha e o que funciona

- **Tensão do núcleo**: SYSVDD padrão 1,26 V (`P3_ANA_CON9` nível 11); usamos 1,32 V (nível 13) quando o
  núcleo 1 trabalha. Laço de espera do núcleo 1 na flash (RAM + RAM trava o chip).
- **Ferramentas de diagnóstico** (todas pelo USB):
  - unidade de debug do chip (`corex2`, 0x1EEE000, do `cpu/wl82/debug.c` do SDK AC79): **janelas de proteção
    de escrita por núcleo** (`WR_LIMITn`, `C1_WR_LIMIT_EN`) e exceções de acesso inválido ao barramento (`DBG_EN`);
  - **registradores do núcleo 1 legíveis pelo núcleo 0** (`q32DSP(1)` em 0x1EEF200: r0..r15, RETS, SP, PC);
  - exceção habilitada no núcleo 1 com registro próprio (registradores + pilha na hora da falha);
  - `tools/jb_c1.py` (JBQ), `tools/jb_crash.py` (JBF), `tools/jb_peek.py` (JBM), `tools/jb_volt.py` (JBV).
- **v0.12.0**: com as janelas, o núcleo 1 foi pego escrevendo **fora dos buffers** dentro de `layer_run`
  (bloco 214, ~0,6 s após ligar o NAM); o pedal seguiu vivo (núcleo 1 parado com segurança).
- **v0.12.3**: retrato do núcleo 1 após uma falha com float: `RETS = 0x02200a32` (certo: `0x02000a32`) — o
  endereço de retorno voltou da pilha **com o bit 21 a mais**, o defeito descrito pelo Jangada.
- **Testes (núcleo 0 rodando o front do NAM em float ao mesmo tempo):**

  | Teste | Núcleo 1 | Resultado |
  |---|---|---|
  | JB6: lê padrões fixos em 4 regiões da RAM | inteiro | 0 erros em ~140 M leituras |
  | JB8: recursão profunda verificada (pilha) | inteiro | 0 erros em 474 mil recursões / 120 s |
  | JB7: cadeia de float verificada | **float** | o pedal cai (watchdog) |
  | pipeline do NAM (v0.10–v0.12.2) | **float** | falha em 0,6–60 s |

- **Conclusão**: nesta unidade **o núcleo 1 é confiável só com inteiros**. Plano: núcleo 0 roda a primeira metade
  do A2-Lite em float; o núcleo 1 roda a segunda metade em **ponto fixo** (pesos Q2.29, ativações Q8.23,
  acumulador 64 bits); o núcleo 0 converte na entrada e na saída (nenhuma instrução de float no núcleo 1).
- Tudo que o núcleo 1 usa (pilhas, mailbox, `nam`, `pipe`) foi para a RAM alta (seção `.c1_bss`, 0x01C50000).
- Um núcleo só não basta: o melhor laço possível (~3 ciclos/MAC) daria ~3050 us por bloco (> 2902).

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
