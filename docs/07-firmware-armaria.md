# 07 – Firmware armaria-nam

Nome provisório: **armaria-nam**. Identidade no protocolo de update: `BlackBox_9xx`
(o modelo continua `BlackBox` para manter compatibilidade com o updater oficial e
com `tools/bb_install.py`).

## v0.1.1 (2026-10-22) — gravado e rodando

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
