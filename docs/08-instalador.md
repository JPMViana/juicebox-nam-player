# 08 – Instalador (Go) e distribuição

Estado em 2026-11-04. Código em `installer/` (módulo Go `juicebox/installer`).

## O que é

- **App gráfico** (Wails v2, `installer/main.go` + `app.go` + `frontend/dist/`, HTML/CSS/JS sem bundler):
  detecta o pedal, instala/atualiza o Juice-Box, gerencia capturas e bancos, volta ao firmware oficial.
  Bilíngue: **inglês padrão** + português (pergunta o idioma na 1ª execução; textos em `frontend/dist/i18n.js`).
- **CLI `jbx`** (`installer/cmd/jbx`): mesmas funções pela linha de comando (`ports`, `info`, `install RELEASE.json`,
  `install-fwsc`, `revert`, `captures`, `put`, `select`, `wipe`, `banks`; `--log ARQ` grava diagnóstico).

| Pacote | Função | Validação |
|---|---|---|
| `internal/jlpkg` | porte do `tools/bbpkg.py` (UFW, ENC/SFC, flash) | pacotes **idênticos byte a byte** ao Python (V20, Juice-Box, bump-cfg) |
| `internal/fwbuild` | monta o `.fwsc` a partir do release + V20 | idêntico ao `fw/build.py` |
| `internal/updater` | porte do `tools/bb_install.py` (handshake, etapas 1 e 2) | testes com pedal simulado |
| `internal/nam` | `.nam` → slot (porte do `nam2c.py`) | idêntico ao Python |
| `internal/store` | capturas e bancos (`d e w c s x r t u`) | testado no pedal (leitura) |
| `internal/stock` | baixa e confere o V20 (cache em `~/Library/Caches/Juice-Box/`) | SHA-256 |
| `internal/midiio` | MIDI (gomidi v2 + rtmidi, cgo) | — |

## Distribuição sem bytes do fabricante

O `app.bin` do Juice-Box contém o **carregador de resgate da JieLi** (copiado do V20). Por isso:

1. `fw/build.py --product BlackBox_NNN --release` gera `build/fw/release/juicebox-<versão>-app.bin` com essa região
   **zerada** + `juicebox-<versão>.json` (offset, tamanho, CRC16 do carregador; SHA-256 do app e do V20).
2. `installer/scripts/build.sh` copia o release para `installer/assets/release/` (embutido via `go:embed`) e roda
   `wails build` (alvo macOS 11+).
3. Na máquina do usuário, o app baixa o V20 (`internal/stock`, hoje de um repositório de terceiros no GitHub —
   **pendente**: achar download oficial da M-VAVE), confere o SHA-256, reinsere o carregador e monta o pacote.
   Sempre com `bump-cfg` (necessário a partir do firmware de fábrica, inofensivo nos outros casos).

Sem assinatura paga (decisão do usuário): ver backlog de documentação em [03-roadmap.md](03-roadmap.md).

## Testes feitos no pedal

- `jbx` (CLI): `info`, `captures`, `banks` ok; **`jbx install` (Juice-Box → Juice-Box) ok** — o carregador só
  regravou o que mudou (~30 leituras).
- Janela: detecção e listagem ok. **Instalação pela janela com problema (abaixo).**

## Bug aberto: a janela fica presa em 99 % na gravação

Sessão de 2026-11-04 00:41 (log em `~/Library/Caches/Juice-Box/installer.log`):
- Reinstalação Juice-Box 980 → 980 pela janela. Etapa 1 ok (75 leituras, `finished true`). O carregador apareceu
  como porta **"USB Composite Device"**, identidade `ota-?` (a porta do Juice-Box é "armaria-nam").
- Etapa 2 começou às 00:42:14 e **nunca terminou**; ~5 min depois `sample` mostrou o app enviando MIDI
  (`rtmidi_out_send_message`). O log daquela versão não registrava cada pedido.
- O usuário fechou o app: **o pedal reiniciou sozinho já com o firmware gravado** (BlackBox_980 rodando).
  Conclusão: o carregador terminou e esperava uma resposta que não reconhecemos (provável pedido de fim
  `0xF0000000` não lido / mensagem SysEx chegando diferente), ou repetia um pedido cuja resposta rejeitava.
- O usuário relatou antes que, instalando a partir do firmware de fábrica, a janela mostrava erro embora o pedal
  estivesse em modo de atualização (a mensagem exata não chegou). Correção já feita: se a etapa 1 termina
  "desconectado"/"parou de responder", espera o carregador (90 s) e segue; a janela oferece "Terminar instalação".

Já corrigido (commit `4f0e2ae`, **não testado no pedal**): o limite de 180 s só conta pedidos válidos; o mesmo
pedido repetido 30× vira erro; o log registra cada pedido (`serve: req ...`) e o conteúdo de mensagens não
reconhecidas (`serve: not a request ...`).

**Próximo passo:** o usuário reinstala pela janela (Firmware → Reinstalar); ler o log e comparar com a CLI
(`jbx install ... --log build/jbx-install.log`). Hipóteses: (1) a SysEx do pedido de fim chega diferente pelo
rtmidi embutido (gomidi usa um RtMidi antigo; mensagens grandes podem ser fragmentadas / truncadas); (2) algo
específico do processo gráfico (threads / CoreMIDI) — a CLI funcionou no mesmo caminho.

Recuperação garantida se o pedal ficar em modo de atualização:
`.venv/bin/python -I tools/bb_install.py build/fw/juicebox-BlackBox_980.fwsc --force --yes`.

## Comandos

```bash
.venv/bin/python fw/build.py --product BlackBox_980 --release   # firmware + release
cd installer && go test ./... && ./scripts/build.sh               # testes + app (build/bin/Juice-Box.app)
go build -o ../build/jbx ./cmd/jbx                                # CLI
```

O usuário grava o pedal pelo `!` no terminal (o modo automático bloqueia a gravação feita pelo agente).

## Pendências da distribuição

- Corrigir o bug acima; testar o caminho completo firmware de fábrica → Juice-Box pela janela; testar "Restaurar".
- Build Windows (GitHub Actions; o rtmidi exige compilar no Windows), checksums no release, ícone próprio.
- Documentação do usuário (EN/PT) para abrir o app sem assinatura (backlog no roadmap).
- Fonte oficial do V20.
