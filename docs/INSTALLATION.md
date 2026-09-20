# Guida Completa all'Installazione & Configurazione di PSP-Forge 🛠️

Questa guida descrive l'intero processo di configurazione dell'ambiente di sviluppo per PlayStation Portable (PSP) su Linux (Ubuntu, Debian, Fedora, Arch e derivate), dall'installazione della toolchain ufficiale **PSPSDK** fino alla configurazione della suite **PSP-Forge** e dell'emulatore **PPSSPP**.

---

## 1. Prerequisiti di Sistema

### Pacchetti di Base
Assicurati di disporre degli strumenti essenziali di compilazione, Git, CMake e Python:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  git \
  python3 \
  python3-pip \
  libusb-dev \
  libreadline-dev \
  ffmpeg
```

### Moduli Python Necessari
PSP-Forge richiede Python 3.10+ e la libreria `Pillow` per il processing delle texture e delle immagini:
```bash
pip install Pillow
```

---

## 2. Installazione della Toolchain PSPSDK (`pspdev`)

Per compilare codice C per l'architettura MIPS Allegrex della PSP è necessario il compilatore cross-platform `psp-gcc`.

### Percorso Standard Consigliato: `/usr/local/pspdev`

Se hai scaricato o compilato la toolchain precompilata, estraila in `/usr/local/pspdev`:
```bash
sudo mkdir -p /usr/local/pspdev
sudo chown -R $USER:$USER /usr/local/pspdev
# (Estrai i binari del toolchain all'interno di /usr/local/pspdev)
```

### Variabili d'Ambiente (`~/.bashrc` o `~/.zshrc`)
Aggiungi le variabili d'ambiente indispensabili al tuo file di configurazione shell:

```bash
# Variabili d'ambiente Sony PSP Toolchain
export PSPDEV="/usr/local/pspdev"
export PATH="$PSPDEV/bin:$PATH"
```

Ricarica il terminale con:
```bash
source ~/.bashrc
```

Verifica che la toolchain risponda correttamente:
```bash
psp-gcc --version
psp-config --pspsdk-path
```

---

## 3. Installazione e Build del Runtime PSP-Forge (`libpspforge`)

PSP-Forge include il micro-runtime in C99 ad alte prestazioni `libpspforge.a` che gestisce VRAM deterministica, display list GPU, audio multithread ed input.

Entra nella directory del runtime e compila la libreria statica:

```bash
cd support_toolchain/psp-forge/runtime

# Compilazione di tutti i moduli C
psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/core.c -o src/core.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/vram.c -o src/vram.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/video2d.c -o src/video2d.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/video3d.c -o src/video3d.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/input.c -o src/input.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/audio.c -o src/audio.o

# Creazione dell'archivio statico .a
psp-ar rcs libpspforge.a src/*.o

# Installazione negli include e nelle lib di sistema della toolchain
sudo cp include/psp_forge.h $PSPDEV/psp/include/psp_forge.h
sudo cp libpspforge.a $PSPDEV/psp/lib/libpspforge.a
```

---

## 4. Installazione della CLI `psp-forge`

Per poter invocare `psp-forge` da qualsiasi cartella di lavoro:

```bash
cd support_toolchain/psp-forge

# Rendi eseguibile il launcher
chmod +x bin/psp-forge

# Crea un symlink in /usr/local/bin
sudo ln -sf "$(pwd)/bin/psp-forge" /usr/local/bin/psp-forge
```

Testa l'installazione digitando:
```bash
psp-forge --help
```

---

## 5. Configurazione dell'Emulatore PPSSPP

### Installazione di PPSSPP
PPSSPP può essere installato tramite il proprio gestore pacchetti, Flatpak, oppure scaricando la versione standalone AppImage:

```bash
# Opzione A: Tramite Flatpak (consigliato per distribuzioni moderne)
flatpak install flathub org.ppsspp.PPSSPP

# Opzione B: Tramite AppImage standalone
# Scarica l'AppImage ufficiale dal sito ppsspp.org e rendila disponibile nel tuo PATH:
chmod +x PPSSPP.AppImage
sudo mv PPSSPP.AppImage /usr/local/bin/ppsspp
```

`psp-forge run` cercherà automaticamente l'eseguibile `ppsspp`, `PPSSPPQt` o l'installazione Flatpak nel tuo sistema. In alternativa, puoi passare il percorso dell'emulatore esplicitamente con:
```bash
psp-forge run --emulator /percorso/del/tuo/ppsspp
```

---

## 6. Verifica Finale del Flusso di Lavoro

Ora puoi creare e testare un gioco con 3 comandi:

```bash
# 1. Crea il progetto da template
psp-forge init test_game --template 2d
cd test_game

# 2. Compila asset ed eseguibile EBOOT.PBP
psp-forge build

# 3. Lancia in emulatore
psp-forge run
```
