# SPE 1.3K-2K-FA Remote — Qt6 port

Native **Qt6 / C++20** port of the WebSocket amplifier-bridge. 

The wire format (WebSocket JSON field names, serial command frames) is kept
**byte-identical** to the original, so the existing ESP8266 device and any
copies of the old HTML client keep working unchanged.

## Two binaries, one codebase

| Binary | Links | What it's for |
|---|---|---|
| `spe-remoted` | `QtCore`, `Network`, `SerialPort`, `WebSockets`, `HttpServer` | **Headless daemon**. Runs on a Raspberry Pi (or any Linux/Windows box) with no display. Serial → WebSocket → browser UI. This is what you install at the amp site. |
| `spe-remote-qt` | all of the above + `Gui`, `Widgets` | **Desktop app** with the native Qt UI that matches the original web screenshot. Opens serial directly. For when you're sitting at the PC attached to the amp. |

Both binaries bundle the original `web/` UI via Qt resources, so any browser
on the network — iPad, Android, another laptop — can reach it at
`http://<host>:8080/` without needing Apache/lighttpd.

## Architecture

```
  ┌───────────────────────────────────────┐
  │  Raspberry Pi (or PC) at the amp      │
  │                                       │
  │                spe-remoted            │
  │  USB ─ AmpController ── WsBridge ─ ws://:8888/ws ──┐
  │            │                                       │
  │            └──── HttpServer ─ http://:8080/ ───────┼─► any browser
  │                                                    │   (iPad, phone,
  │                                                    │    another PC…)
  └────────────────────────────────────────────────────┘
```

## New-user install guides

If you're getting set up for the first time, the friendly walkthroughs
below cover everything (apt deps, group membership, systemd, common
errors) end-to-end:

- **Linux desktop / server** → [`INSTALL-LINUX.md`](INSTALL-LINUX.md)
  ([upgrading from an older version](INSTALL-LINUX.md#updating-to-a-newer-release))
- **Raspberry Pi (headless)** → [`INSTALL-RPI.md`](INSTALL-RPI.md)
  ([upgrading from an older version](INSTALL-RPI.md#updating-to-a-newer-release))
- **Windows** → [`INSTALL-WINDOWS.md`](INSTALL-WINDOWS.md) — grab the
  pre-built zip from
  [the latest release](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest)
  and unzip. To upgrade, unzip the new release over the old folder.

The sections below are the terse reference for people who already know
their way around CMake.

## Build

Requires **Qt 6.4+** with `SerialPort`, `WebSockets`, and `HttpServer`
modules. `HttpServer` is in a separate package on most distros.

### Linux / Raspberry Pi OS (bookworm+)

```bash
sudo apt-get install -y cmake ninja-build build-essential \
    qt6-base-dev qt6-serialport-dev qt6-websockets-dev qt6-httpserver-dev

cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Windows

Use the Qt online installer and tick `Qt Serial Port`, `Qt WebSockets`, and
`Qt HTTP Server`. From a "Qt 6.x (MSVC 2022 64-bit)" shell:

```powershell
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS

```bash
brew install qt6 cmake ninja
cmake -B build -S . -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)"
cmake --build build -j
```

## Run

### Desktop GUI (dev / bench use)

```bash
./build/spe-remote-qt
# …pick serial port from the toolbar, click Connect.

# Or fully non-interactive:
./build/spe-remote-qt --serial-port COM4 --autostart
```

### Headless daemon

```bash
# First run: serial port can be left unset — the browser Settings page
# will pick one and persist it.
./build/spe-remoted

# Or pin a device at launch (overrides the saved config for this run):
./build/spe-remoted --serial-port /dev/ttyUSB0

./build/spe-remoted --list-ports        # list available devices
./build/spe-remoted --help              # all flags
```

Once the daemon is running, every client on your LAN can reach the UI:

- **Any desktop browser** → `http://<server-ip>:8080/`
- **iPad / iPhone / Android** → same URL, add it to the Home Screen for a
  native-feeling app icon.


### Configuring remotely (Settings page)

The top bar of the web UI shows a connection LED and a ⚙ cog that opens
**Settings** (`/settings.html`). From there the daemon can be reconfigured
entirely over HTTP — no SSH required:

- **Serial device** — dropdown of every port the OS sees, with the FTDI
  chip used by SPE amps marked **★** so a fresh Pi boots to the right one.
- **Baud rate** — defaults to 115200 (the SPE Expert factory setting).
- **Status LED** — green when open, red when the port is busy / missing /
  permission-denied, with the underlying error text inline so you don't
  have to hunt through logs.
- **Apply / Disconnect** — writes `config.json` (see below) and reopens
  the port; the daemon will re-open the same device on next boot.

Persisted config lives in `QStandardPaths::AppConfigLocation`:

| Platform | Path |
|---|---|
| Linux (user) | `~/.config/spe-remote/config.json` |
| Linux (systemd service) | `/var/lib/spe-remote/spe-remote/config.json` |
| Windows | `%APPDATA%\spe-remote\config.json` |
| macOS | `~/Library/Preferences/spe-remote/config.json` |

### REST API

Mostly for the browser UI, but scriptable if you want to automate:

| Method | Path | Body | Returns |
|---|---|---|---|
| GET  | `/api/ports`      | — | JSON array of `{name, description, manufacturer, vid, pid, likelySpe}` |
| GET  | `/api/config`     | — | `{port, baud, connected, lastError}` |
| POST | `/api/config`     | `{"port":"COM4","baud":115200}` | `{ok: true}` or `{ok: false, error: "..."}` |
| POST | `/api/connect`    | — | `{ok: true}` — reopen the serial port |
| POST | `/api/disconnect` | — | `{ok: true}` — close the serial port |

Example:

```bash
curl http://pi.local:8080/api/ports
curl -X POST http://pi.local:8080/api/config \
     -H 'content-type: application/json' \
     -d '{"port":"/dev/ttyUSB0","baud":115200}'
```

## Raspberry Pi one-shot install

```bash
git clone <this repo>
cd qt6-port
sudo ./packaging/install-pi.sh
```

Installs Qt build deps, compiles only the daemon, drops the binary at
`/usr/local/bin/spe-remoted`, installs a systemd unit, and starts the
service. Logs via `journalctl -u spe-remoted -f`.

Edit `/etc/systemd/system/spe-remoted.service` to change the serial device
or ports; `systemctl daemon-reload && systemctl restart spe-remoted` to
apply.

## Reaching the amp from outside the LAN

**Do not port-forward `:8888` or `:8080` to the public internet.** There is
no authentication — anyone reaching the WebSocket can key your PA.

The right answers, in order of effort:

1. **VPN** (Tailscale or WireGuard) on both the Pi and the mobile device.
   Zero code changes, full LAN access anywhere. This is how I deploy it.
2. **Cloudflare Tunnel** with Cloudflare Access (identity check) in front.
3. Wait for TLS + basic-auth to be added here (planned, not implemented yet).

## Front-panel controls

The synthetic chassis (Qt face + browser UI) exposes every key from the
SPE Application Programmer's Guide §4 plus a community power-on hack:

**Primary cluster** — the keys you reach for during a QSO:

| Key | Wire token | Opcode | What it does |
|---|---|---|---|
| INPUT | `input`   | `0x01` | Cycle radio input |
| ANT   | `antenna` | `0x04` | Cycle TX antenna |
| TUNE  | `tune`    | `0x09` | Tune ATU |
| PWR   | `gain`    | `0x0B` | Cycle PWR level (HIGH / MID / LOW) |
| CAT   | `cat`     | `0x0E` | Toggle CAT / RIG-control |
| OP    | `oper`    | `0x0D` | Operate / Standby — tints amber while Operate |
| ON    | `on`      | DTR pulse | Power on — pulses DTR ~300 ms (community workaround; no opcode exists) |
| OFF   | `off`     | `0x0A` | Power off — confirms first |

**Secondary "menu / setup" strip** — small buttons centered below the
main panel; tuning-time keys, not for routine operate:

| Key | Wire token | Opcode | What it does |
|---|---|---|---|
| ←   | `left`    | `0x0F` | LEFT arrow (menu navigation) |
| →   | `right`   | `0x10` | RIGHT arrow (menu navigation) |
| SET | `set`     | `0x11` | SET — enter / accept setup screens |
| DISP | `display` | `0x0C` | DISPLAY backlight toggle |
| L−  | `l-`      | `0x05` | ATU inductor trim DOWN |
| L+  | `l+`      | `0x06` | ATU inductor trim UP |
| C−  | `c-`      | `0x07` | ATU capacitor trim DOWN |
| C+  | `c+`      | `0x08` | ATU capacitor trim UP |

Send any wire token over the WebSocket and the daemon executes it.
Any ESP32 or older devices and old web clients continue to work — the original
five tokens (`oper` / `antenna` / `input` / `tune` / `gain`) are
unchanged.

## Fetures 

- Single-threaded Qt event loop; no `thread.start_new_thread`, no global
  mutable state.
- Auto-reconnect on serial errors (2 s backoff).
- Serial device and listen ports configurable at runtime (CLI + GUI +
  browser Settings page), persisted to JSON — no hard-coded `/dev/ttyUSB0`.
- HTTP server built in — no separate Apache / lighttpd needed to serve the
  web UI.
- Web client auto-derives its WebSocket URL from `window.location.host`, so
  no more hand-editing `index.html` per deployment.
- Native Qt desktop UI that matches the original screenshot, as an
  alternative to the browser UI.


