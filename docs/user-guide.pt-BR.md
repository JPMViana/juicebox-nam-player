# Guia do usuário do Juice-Box

[English](user-guide.md)

O Juice-Box é um firmware livre e de código aberto que transforma o **M-VAVE ANN BlackBox** em um player de
capturas **NAM (Neural Amp Modeler) A2**. Você o instala, e volta ao firmware oficial, pelo app Juice-Box no macOS
ou no Windows, usando o cabo USB do pedal. Não é preciso nenhum hardware extra.

> ⚠️ **Projeto não oficial.** Não é feito nem suportado pela M-VAVE. Instalar firmware é por sua conta e risco e pode
> anular a garantia. O firmware oficial pode ser restaurado a qualquer momento pelo app.

## O que você precisa

- Um M-VAVE ANN BlackBox com bateria carregada.
- Um **cabo USB de dados**. Alguns cabos USB-C só carregam: se o app nunca acha o pedal, troque o cabo.
- Um computador com **macOS 11 ou mais novo** (Apple Silicon ou Intel) ou **Windows 10/11 64 bits**, com internet.
  O app baixa o firmware oficial da internet para montar o pacote de instalação.
- Capturas NAM **A2** (arquivos `.nam`). Veja [Capturas](#4-capturas).

## 1. Baixar

Abra o [release mais recente](https://github.com/JPMViana/juicebox-nam-player/releases/latest) e baixe:

| Sistema | Arquivo |
|---|---|
| macOS | `Juice-Box-<versão>-macOS.zip` |
| Windows | `Juice-Box-<versão>-Windows.exe` |

**Opcional: confira o download.** Compare o SHA-256 com a linha correspondente no `SHA256SUMS.txt` da mesma página:

- macOS (Terminal): `shasum -a 256 ~/Downloads/Juice-Box-*-macOS.zip`
- Windows (PowerShell): `Get-FileHash $HOME\Downloads\Juice-Box-*-Windows.exe`

## 2. Abrir o app (ele não é assinado)

O app não tem certificado pago da Apple ou da Microsoft, então na primeira vez o sistema avisa que não conhece o
desenvolvedor. Isso é esperado. Todo o código-fonte está neste repositório.

### macOS

1. Clique duas vezes no `.zip` para extrair o `Juice-Box.app` e arraste-o para **Aplicativos**.
2. Abra o app. Se o macOS disser que ele "não pode ser aberto" ou que "não foi possível verificar o desenvolvedor":
   - **macOS 15 (Sequoia) ou mais novo:** clique em **OK**, abra **Ajustes do Sistema → Privacidade e Segurança**,
     desça até *"Juice-Box foi bloqueado…"*, clique em **Abrir Mesmo Assim** e confirme com a senha.
   - **macOS 14 ou anterior:** clique com o botão direito (ou Control-clique) no app → **Abrir** → **Abrir**.
3. Se o macOS disser que o app "está danificado", é a quarentena do download. Rode uma vez no Terminal:
   `xattr -dr com.apple.quarantine /Applications/Juice-Box.app`

Isso só é necessário uma vez.

### Windows

1. Clique duas vezes em `Juice-Box-<versão>-Windows.exe`.
2. Se aparecer **"O Windows protegeu o computador"** (SmartScreen), clique em **Mais informações → Executar assim mesmo**.
3. O app usa o Microsoft Edge WebView2, que já vem no Windows 10/11. Se faltar, o próprio app oferece a instalação.

## 3. Instalar o Juice-Box

1. Ligue o pedal e conecte-o **direto** no computador (sem hub).
2. **Feche outros apps que usem o pedal ou MIDI**, como o editor da M-VAVE ou uma DAW. No Windows, só um app por vez
   pode abrir um aparelho MIDI.
3. Abra o Juice-Box. Na primeira vez ele pergunta o idioma (English ou Português). Dá para trocar depois, no canto
   superior direito.
4. O app mostra **"BlackBox com o firmware oficial"**. Clique em **Instalar Juice-Box**, leia o aviso e confirme.
5. Espere cerca de um minuto. O pedal reinicia algumas vezes. **Não desconecte o pedal nem feche a janela durante a
   gravação.**
   - A barra de progresso pode **ficar um tempo em 99 %**. É um bug conhecido. Espere alguns minutos. Se o pedal
     reiniciar mostrando o Juice-Box, a instalação terminou e você pode fechar o app.
6. Quando o pedal mostrar a tela do Juice-Box, está pronto.

Suas capturas e bancos continuam no pedal quando você atualizar o Juice-Box depois com um app mais novo.

## 4. Capturas

O Juice-Box toca capturas **NAM A2** (o arquivo "A2" completo ou "A2-Lite"). Ele roda a parte A2-Lite do modelo, a
que cabe no chip do pedal. **Capturas NAM antigas ou padrão (A1, "WaveNet standard", "lite", "feather") não
funcionam**, e o app avisa quando um arquivo não é A2.

Onde conseguir: no [TONE3000](https://www.tone3000.com) (filtre pela arquitetura **A2**), ou treine as suas com o NAM.

Quais escolher:
- **Num power amp** (retorno de efeitos de um amplificador, ou power amp + caixa de verdade): use capturas **só do
  amplificador** (sem caixa). O Juice-Box foi feito para isso.
- **Em fone, interface de áudio ou caixa full-range**: use capturas **com a caixa** ("amp + cab", "full rig"). O
  Juice-Box ainda não carrega IRs.

No app:
- Aba **Capturas** → **Adicionar capturas…**: escolha até 8 arquivos `.nam`. Gravar reinicia o pedal (cerca de 2 s).
  O volume é normalizado como no plugin do NAM.
- Aba **Bancos**: cada botão de banco do pedal (**FX, AMP, MOD, DLY, REV, CAB**) guarda duas capturas, **A** e **B**,
  com níveis próprios de **IN** (entrada) e **OUT** (saída). Clique em **Salvar no pedal**.

## 5. Usando o pedal

| Controle | O que faz |
|---|---|
| **FX AMP MOD DLY REV CAB** | escolhe o banco 1–6 |
| **Footswitch direito** | alterna entre as capturas A e B do banco |
| **Footswitch esquerdo**, toque | efeito liga / desliga (desligado = sinal seco) |
| **Footswitch esquerdo**, segurar | mudo (outra pisada tira o mudo) |
| **MASTER** (knob esquerdo) | volume de saída |
| **PARA** (knob direito), clique | menu do banco: banco, capturas A / B, IN, OUT, idioma, salvar. Gire para navegar ou mudar |
| **PARA**, segurar 1 s | salvar (bancos e o banco inicial). O pedal reinicia rapidinho |
| **HOME** | tela de informações (versão, capturas, bateria). Aperte de novo para voltar |

## 6. Voltar ao firmware oficial

No app: aba **Firmware** → **Restaurar firmware oficial…**. O app baixa o firmware oficial V20 da M-VAVE, confere,
apaga as capturas do Juice-Box e instala o V20. Depois disso o pedal volta a funcionar com o editor da M-VAVE.

## Problemas comuns

| Problema | O que fazer |
|---|---|
| "Conecte seu BlackBox" não muda | Troque o cabo USB (precisa ser de dados), ligue direto sem hub, feche o editor da M-VAVE e DAWs, desligue e ligue o pedal. |
| macOS: "danificado" ou "não pode ser aberto" | Veja [Abrir o app](#2-abrir-o-app-ele-não-é-assinado). |
| Parado em 99 % | Espere alguns minutos. Se o pedal mostrar o Juice-Box, feche o app. Se não, veja a próxima linha. |
| "O pedal está em modo de atualização" | Uma instalação foi interrompida. Clique em **Terminar instalação**. Nesse modo o pedal está seguro, esperando um pacote. |
| "Não foi possível obter o firmware oficial" | O app precisa de internet na primeira vez (depois guarda uma cópia). |
| Chiado ou zumbido só com o cabo USB ligado | É loop de terra: o pedal está no mesmo computador da sua interface de áudio. Tire o USB depois de configurar, ou use o pedal na bateria. |
| Captura "não é NAM A2 / A2-Lite" | Use capturas A2 (veja [Capturas](#4-capturas)). |
| Captura "alta demais para a matemática do pedal" | Muito raro. Tente outra captura. |

## Relatar um problema

[Abra uma issue](https://github.com/JPMViana/juicebox-nam-player/issues/new/choose) (pode ser em português) e anexe o log do app:

- macOS: `~/Library/Caches/Juice-Box/installer.log` (Finder → Ir → Ir para a Pasta…)
- Windows: `%LOCALAPPDATA%\Juice-Box\installer.log` (cole na barra de endereço do Explorador)

Diga o sistema, a versão do app e o que a tela do pedal mostrava.

## Limitações conhecidas

- O pedal roda a 44,1 kHz e as capturas são feitas a 48 kHz, então o timbre pode diferir um pouco do plugin.
- Ainda sem IR / simulação de caixa.
- A saída PHONES ainda não foi testada.
- A bateria pode mostrar 97 % quando está cheia.
- Salvar e gravar capturas reinicia o pedal (1–2 s).
