# 04 – Estratégia só-USB (sem gravador externo)

**Decisão do projeto (2026-10-22): nenhuma compra de gravador SPI, clip SOIC-8
ou dongle.** Todo o desenvolvimento, gravação e recuperação acontece pelo cabo
USB-C do próprio pedal.

## Por que isso é viável

A plataforma do BlackBox (JieLi AC791N, mesmo SPL `uboot.boot`, mesmo
carregador de atualização `ota.bin`, mesma chip-key `0x980F`) é a mesma do
M-VAVE FM-1 e do SMK-37. Nesses aparelhos, a comunidade já instala firmwares
**completos e de código aberto só por USB**, sem nenhum hardware extra:

| Projeto | Aparelho | Instalação | Volta ao firmware de fábrica |
|---|---|---|---|
| [Felucca](https://github.com/hugelton/Felucca) (GPL-3.0) | FM-1 | instalador web (WebMIDI) | sim, pelo instalador ou pelo M-UPGRADE |
| [fm1-x0x](https://github.com/charlesvestal/fm1-x0x) | FM-1 | instalador web | sim + **modo de segurança** automático se o app ficar travando |
| [Snoop37 / fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) | SMK-37 | instalador web | sim — **é um port de firmware FM-1 para outra placa AC791N**, exatamente o nosso caso |
| [annblackbox-custom-firmware-installer](https://github.com/entitymar/annblackbox-custom-firmware-installer) | **BlackBox** | M-UPGRADE modificado | restaura o V20 de fábrica |

Além disso, a própria M-VAVE publicou o [m-vave/fm1_open](https://github.com/m-vave/fm1_open)
com drivers em C e gravação por USB.

### Como a gravação por USB funciona (FM-1-RE, confirmado em hardware)

1. O **app em execução** recebe o comando de update via USB-MIDI.
2. Ele baixa o `ota.bin` (carregador, ≤ 19 456 bytes, múltiplo de 512) e confere
   **apenas CRC-16** — sem assinatura criptográfica.
3. Reinicia no carregador (roda em RAM em `0x01C0A800`), que grava o novo
   `app.bin` na flash e confirma com `0xE0000000` / `0xF0000000` + `"success\0"`.

### Confirmado no BlackBox (2026-10-22)

- V20 oficial gravado pelo `tools/bb_install.py` no macOS (009 → 020).
- Pacote gerado por `tools/bbpkg.py` com app modificado aceito e executado (020 → 019).
- Para pacotes cujo `cfg` é igual ao instalado, usar `bbpkg.py build --bump-cfg`.
- Voltar ao oficial: `bb_install.py firmware/stock/BlackBox_FACTORY_V20.fwsc`.

## O risco que sobra (e como eliminamos)

O único cenário sem saída por USB é: **nosso app grava certinho, passa no CRC,
mas trava antes de inicializar o USB.** Aí o pedal não aceita update e a ROM
não entra em modo de recuperação (porque a flash "parece" válida).

Regras para que isso não aconteça:

1. **USB e rotina de update são a primeira coisa que o nosso firmware inicializa**,
   antes de codec, NAM, display ou footswitches. Reaproveitar o código de USB/update
   do Felucca, que já funciona em campo.
2. **Modo de segurança:** contador de boots + watchdog. Se o app reiniciar N vezes
   seguidas, ou se um footswitch estiver pressionado ao ligar, sobe só o USB/update.
3. **Validar o hardware antes de gravar o app** (ver "Fase 1" no roadmap): primeiro
   rodar código de teste que não substitui o app de fábrica.
4. **Nunca tocar** em `uboot.boot`, `key_mac` (`0xFF000`), `BTIF` e calibração de
   fábrica (`USRTRIM`/`trimdat` no SMK-37). Só o `app.bin` (e, se preciso, o `ota.bin`).
5. Toda ferramenta tem `--dry-run`, confere CRC/tamanho/alinhamento e lê de volta.
6. Durante a gravação: pedal ligado na fonte, cabo USB direto (sem hub).

## Último recurso sem compras (não planejado, apenas documentado)

A ROM do JieLi entra sozinha em modo de boot USB quando **não consegue ler a
flash** ([kagaimiq/jielie](https://kagaimiq.github.io/jielie/isp/usb/usb-key.html)).
Em tese, abrir o pedal e impedir a leitura da flash por um instante ao ligar
(ex.: com uma pinça) colocaria o chip nesse modo, e daí a flash poderia ser
regravada pelo PC. **Não testado no AC791N**; só seria considerado em caso
de brick real.
