# Installing on Windows

Pre-built binaries are published with each release — no Qt SDK or
compiler required.

> **Tested on:** Windows 10 and 11 (x64). All Qt 6 runtime DLLs are
> bundled; nothing else needs installing.

## 1. Download

Grab the latest zip from
[the Releases page](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest):

```
spe-remote-qt-<version>-windows-x64.zip
```

## 2. Unzip anywhere

Right-click → **Extract All…**, or use any zip tool. A folder like
`spe-remote-qt-1.3.0-windows-x64\` is created with everything inside —
two `.exe`s, the Qt DLLs, and the platform plugins.

You can put it on the Desktop, in `C:\Program Files\` (with admin
rights), or anywhere else. The app is portable — no installer.

## 3. Run

Double-click **`spe-remote-qt.exe`** for the desktop GUI.

* The synthetic chassis opens. Click the **⚙** at the bottom strip to
  open **Settings**.
* Pick your USB-serial **COM port** (FTDI ports used by SPE amps are
  marked **★**) and the **baud rate** (default `115200`).
* Click **Apply** — the connection LED on the chassis goes green.

The browser UI is automatically served at
[`http://localhost:8080/`](http://localhost:8080/) while the app is
running, so any phone or tablet on the same Wi-Fi can reach it via
`http://<your-pc-ip>:8080/`.

### Headless daemon

If you want a server-only setup (no GUI window), run **`spe-remoted.exe`**
from a `cmd` or PowerShell window:

```powershell
spe-remoted.exe --list-ports        # see what COM ports exist
spe-remoted.exe --serial-port COM4  # bind a specific port
spe-remoted.exe --help              # all flags
```

The daemon serves the web UI at the same `http://localhost:8080/`.
Closing the console window stops the daemon.

## 4. First-run notes

* **SmartScreen** may flag the binary on first launch ("Windows
  protected your PC"). The release zip is unsigned. Click **More info →
  Run anyway** if you trust the source.
* **COM port permissions** — the FTDI driver from the amp's USB-serial
  cable is required. If `--list-ports` shows nothing, install the FTDI
  VCP driver from [ftdichip.com](https://ftdichip.com/drivers/vcp-drivers/).
* **`HTTP listen failed: The address is protected`** — Hyper-V /
  WinNAT reserves blocks of ports (commonly including 8080) on Windows
  boxes with Docker Desktop, WSL2, or Hyper-V enabled, even when nothing
  is actually listening there. Open the desktop app's **Connection
  Settings** dialog and change **HTTP port** to something free
  (`8500` works on most machines); the daemon persists it to
  `config.json` so it sticks across restarts. Or run
  `spe-remoted.exe --http-port 8500` from the command line.
  Check what's reserved with
  `netsh interface ipv4 show excludedportrange protocol=tcp`.
* Saved settings live at `%APPDATA%\spe-remote\config.json`.

## Adding a password (optional)

If you'll be reaching the daemon from another machine on the network,
turn on HTTP Basic auth. From `cmd` or PowerShell in the unzipped
folder:

```powershell
spe-remoted.exe --hash-password "your-password-here"
# pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP...
```

Open `%APPDATA%\spe-remote\config.json` and add:

```json
{
  "auth_user": "operator",
  "auth_password_hash": "pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP..."
}
```

Restart the app. Load the web UI as
`http://operator:your-password-here@host:8080/` the first time — the
browser carries the credentials into the WebSocket upgrade only if
they're in the URL.

For HTTPS / WSS, set `cert_file` + `key_file` in the same config —
see the project README's **"Securing the daemon"** section.

## Updating to a newer release

Download the new zip and **unzip it over the old folder**, replacing
the `.exe`s and DLLs. Your saved settings under `%APPDATA%\spe-remote\`
are untouched.

If `spe-remote-qt.exe` was running, close it first (otherwise Windows
refuses to overwrite a running executable).

## Building from source on Windows

Only needed if you want to modify the code. Install the Qt online
installer from [qt.io](https://www.qt.io/download-qt-installer-oss),
tick **Qt 6.4+**, **Qt Serial Port**, **Qt WebSockets**, and **Qt HTTP
Server**, plus a CMake-capable compiler (MSVC 2022 or MinGW).

From a Qt command prompt:

```powershell
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The two binaries land in `build\spe-remote-qt.exe` and
`build\spe-remoted.exe`. Use `windeployqt` against each one to bundle
the runtime DLLs if you want to copy them off the dev machine.
