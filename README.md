# SPE 1.3K-2K-FA Remote — Qt6 port

[![CI](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/actions/workflows/ci.yml/badge.svg)](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/lmacc/SPE-Expert-Amplifier-Remote-Webserver)](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Native **Qt6 / C++20** port of the WebSocket amplifier-bridge.

## Screenshots

**Qt desktop app** — the synthetic chassis with the native front panel
(Windows 11 shown):

![Qt desktop app](docs/qt%20desktop%20app.png)

**Browser UI** — same chassis, same controls, served by the daemon at
`http://<host>:8080/`. Reaches any phone, tablet, or PC on the LAN:

![Web UI](docs/Web%20UI.png)

**Connection settings** — pick the serial port, baud, and listen ports
from the desktop app or the in-browser settings page:

![Connection settings](docs/Connection%20setting%20dialog.png)


The wire format (WebSocket JSON field names, serial command frames) is kept
**byte-identical** original, so any ESP configured device can connect to the webserver.

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

- **Windows** → [`INSTALL-WINDOWS.md`](INSTALL-WINDOWS.md) — pre-built
  zip, all Qt 6 DLLs bundled.
- **macOS** (Apple Silicon and Intel) → [`INSTALL-MACOS.md`](INSTALL-MACOS.md)
  — pre-built tarball, plus the Homebrew build-from-source recipe.
- **Linux desktop / server** → [`INSTALL-LINUX.md`](INSTALL-LINUX.md)
  ([upgrading from an older version](INSTALL-LINUX.md#updating-to-a-newer-release))
- **Raspberry Pi (headless)** → [`INSTALL-RPI.md`](INSTALL-RPI.md)
  ([upgrading from an older version](INSTALL-RPI.md#updating-to-a-newer-release))

Per-release changes are tracked in [`CHANGELOG.md`](CHANGELOG.md).

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

## Securing the daemon

The HTTP server and WebSocket bridge support **HTTP Basic auth** and
**optional TLS**. Both are off by default — fine for a LAN behind a
home router. Turn them on before exposing the daemon to anything else.

For full **remote access from outside the LAN** (operating from a
hotel, work, your phone on cellular), the recommended setup is
Cloudflare Tunnel + Cloudflare Access — free, no port forwarding,
no exposed home IP, real HTTPS with SSO/email login. Walkthrough in
[`REMOTE-ACCESS.md`](REMOTE-ACCESS.md). Tailscale and a self-hosted
Let's Encrypt path are documented there too.

### LAN trust (on by default)

When a password **is** set, peers on a private network (RFC1918
ranges `10/8`, `172.16/12`, `192.168/16`, plus loopback, IPv4
link-local `169.254/16`, IPv6 link-local `fe80::/10`, and IPv6 ULA
`fc00::/7`) skip the password prompt automatically. The operator at
home, on the same Wi-Fi as the Pi, never has to type anything; only
clients reaching the daemon from outside the LAN — port forward,
public tunnel, VPN with public routing — get the 401 prompt.

Disable this if you want every request authenticated (e.g. on a
guest VLAN that's technically RFC1918 but you don't trust):

```json
{ "trust_lan": false }
```

…or pass `--no-lan-trust` on the daemon CLI.

### Step 1 — Add a password

Generate a hash, no plaintext on disk:

```bash
spe-remoted --hash-password "your-password-here"
# pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP...
```

Drop the line into `config.json`:

```json
{
  "auth_user": "operator",
  "auth_password_hash": "pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP..."
}
```

`systemctl restart spe-remoted` and every **non-LAN** request now
needs Basic auth. From the same network as the Pi, browsers reach
the UI without a prompt; from outside, browsers will pop up the
Basic auth dialog. Programmatic / cross-network clients can pass
credentials in the URL (`http://operator:pw@pi.example.com:8080/`)
or set the `Authorization: Basic` header directly.

### Step 2 — Add TLS

For internet exposure (or anywhere you don't trust the link), turn on
HTTPS+WSS. Generate a self-signed cert valid for 5 years:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /var/lib/spe-remote/key.pem \
  -out    /var/lib/spe-remote/cert.pem \
  -days 1825 \
  -subj "/CN=spe-remote.local"
sudo chown spe-remote:spe-remote /var/lib/spe-remote/{key,cert}.pem
sudo chmod 600 /var/lib/spe-remote/key.pem
```

Wire them into `config.json`:

```json
{
  "cert_file": "/var/lib/spe-remote/cert.pem",
  "key_file":  "/var/lib/spe-remote/key.pem"
}
```

The daemon now serves `https://` on port 8080 and `wss://` on port
8888. Self-signed certs trigger a browser warning the first time —
accept it once and it's cached. For a public-facing setup, get a real
cert from Let's Encrypt and point `cert_file` / `key_file` at the
issued PEMs.

### Step 3 — Verify

```bash
curl https://operator:pw@pi.local:8080/api/health
# {"ok":true,"connected":true,"secure":true,"auth":true,"trustLan":true}
```

`/api/health` is intentionally unauthenticated so uptime checks
(systemd `ExecStartPost`, Prometheus blackbox-exporter, etc.) keep
working without a password.

### When to reach for what

| Scenario | Recommended setup |
|---|---|
| Same Wi-Fi as the amp, only one operator | Defaults — no auth, no TLS. |
| Multiple operators on the home LAN | Auth on, TLS off. |
| Reachable via VPN (Tailscale / WireGuard) | Auth on, TLS off (the VPN handles encryption). |
| Reachable via Cloudflare Tunnel + Access | Auth off, TLS off (Cloudflare handles both). |
| Direct internet exposure | Auth **and** TLS, real cert, audit your firewall. |

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


