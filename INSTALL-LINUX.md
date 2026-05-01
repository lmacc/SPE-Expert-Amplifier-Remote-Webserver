# Installing on Linux (desktop & server)

A new-user walkthrough for getting `spe-remote-qt` (desktop GUI) and/or
`spe-remoted` (headless daemon) running on a Linux box. For Raspberry Pi
follow [`INSTALL-RPI.md`](INSTALL-RPI.md) — it's the same idea but with a
one-shot installer.

## What you're building

| Binary | What it does |
|---|---|
| **`spe-remote-qt`** | Desktop app with the Qt-drawn amp face — buttons, LCD, RX/TX LEDs. Use this when you're sitting at a Linux PC plugged into the amp. |
| **`spe-remoted`** | Headless daemon — same logic, no window. Talks to the amp on USB-serial and bridges it to a WebSocket (`:8888`) and a built-in HTTP server (`:8080`) that serves the browser UI. |

Both binaries build from the same source tree. On a server / Pi you only
need the daemon. On a desktop box you can build both and use whichever
one fits the moment.

## Expectations before you start

- **Qt 6.4 or newer** is required. Most distros ship it: Debian 12+,
  Ubuntu 22.04+ (Qt 6.2 — see *Older Qt* below), Ubuntu 24.04 (Qt 6.4),
  Fedora 38+, Arch, openSUSE Tumbleweed.
- **Your user must be in the `dialout` group** to open the USB-serial
  port without `sudo`. The build doesn't need root, but the `usermod`
  step does.
- **No authentication is implemented.** The daemon binds to all
  interfaces by default. On a LAN you trust this is fine; do **not**
  port-forward `:8080` or `:8888` to the public internet. Use Tailscale
  / WireGuard / a Cloudflare Tunnel instead.
- **The web UI and the desktop GUI are functionally equivalent.** Both
  speak to the same daemon; pick whichever you prefer.

## 1. Install build dependencies

### Debian 12, Ubuntu 24.04, Raspberry Pi OS bookworm+

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    cmake ninja-build build-essential pkg-config \
    qt6-base-dev qt6-serialport-dev qt6-websockets-dev qt6-httpserver-dev
```

### Fedora 38+

```bash
sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtserialport-devel \
    qt6-qtwebsockets-devel qt6-qthttpserver-devel
```

### Arch / Manjaro

```bash
sudo pacman -S --needed cmake ninja base-devel \
    qt6-base qt6-serialport qt6-websockets qt6-httpserver
```

## 2. Get the source

```bash
git clone https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver.git
cd SPE-Expert-Amplifier-Remote-Webserver/qt6-port
git checkout v1.3.0       # or stay on main for the latest
```

## 3. Build

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

You'll get two executables in `build/`:

- `build/spe-remote-qt` — built only if Qt6 Widgets is installed (it
  usually is, since `qt6-base-dev` pulls it in).
- `build/spe-remoted`  — always built.

If CMake reports `Qt6 Widgets/Gui not found`, the daemon will still
build; just install the Widgets package and re-run CMake to add the
desktop GUI.

## 4. Grant serial-port access

The amp shows up as `/dev/ttyUSB0` (FTDI cable) or `/dev/ttyACM0`. By
default only root can open it. Add yourself to the `dialout` group:

```bash
sudo usermod -a -G dialout "$USER"
```

**Log out and log back in** for the new group to take effect, or run
`newgrp dialout` for a single shell.

## 5. Run

### Desktop GUI

```bash
./build/spe-remote-qt
```

The amp face opens. Click the **⚙ cog** at the bottom to pick a serial
port (`/dev/ttyUSB0` is the default for SPE amps), set the baud rate
(115200), and click Connect. While the desktop app is running, the
embedded HTTP server is also serving the same UI at
`http://localhost:8080/` for any other device on your LAN.

### Headless daemon

```bash
./build/spe-remoted
```

By default the daemon doesn't open the serial port until you tell it
which device to use — click ⚙ in a browser at `http://localhost:8080/`,
pick the port, click Connect. The choice is saved to
`~/.config/spe-remote/config.json` and reopened on the next launch.

To pin a port at the CLI:

```bash
./build/spe-remoted --serial-port /dev/ttyUSB0
```

Other useful flags:

```bash
./build/spe-remoted --help          # all options
./build/spe-remoted --list-ports    # what serial devices the OS sees
./build/spe-remoted --http-port 0   # disable the embedded web UI
```

## 6. (Optional) Run the daemon as a systemd service

For a server / always-on Linux box you probably want the daemon to start
on boot. The repo ships a unit file at
[`packaging/spe-remoted.service`](packaging/spe-remoted.service) — copy
it, edit the `User=` line if needed, and install:

```bash
sudo install -m 0755 build/spe-remoted /usr/local/bin/spe-remoted
sudo cp packaging/spe-remoted.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now spe-remoted.service

systemctl status spe-remoted          # health
journalctl -u spe-remoted -f           # live log
```

When running under systemd the config file moves to
`/var/lib/spe-remote/spe-remote/config.json` (the unit sets
`XDG_CONFIG_HOME` so the sandbox can write there).

## Updating to a newer release

The current release tag is shown on
[the Releases page](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest);
substitute it for `v1.3.0` below.

```bash
cd ~/SPE-Expert-Amplifier-Remote-Webserver/qt6-port    # wherever you cloned it
git fetch --tags
git checkout v1.3.0                    # or whatever the new tag is
cmake --build build -j                 # incremental rebuild
```

That gives you fresh binaries in `build/`. Run them straight from
there, or if you installed `spe-remoted` as a systemd service, copy the
new binary in and bounce the service:

```bash
sudo install -m 0755 build/spe-remoted /usr/local/bin/spe-remoted
sudo systemctl restart spe-remoted
spe-remoted --version                  # confirm the new version
```

Your saved config (`~/.config/spe-remote/config.json`, or
`/var/lib/spe-remote/spe-remote/config.json` under systemd) is left
alone, so the daemon comes back up on the same serial device.

**Then hard-refresh the browser** (Ctrl-Shift-R, or Cmd-Shift-R on
Mac) so it drops the old cached HTML/CSS — every release re-bundles the
web UI into the binary, so a normal refresh might still show the
previous UI.

If `git checkout` complains about local edits, `git stash` them first
and re-pop afterwards.

## Troubleshooting

**`Permission denied` opening `/dev/ttyUSB0`** — you didn't log out
after `usermod -a -G dialout`, or you typed the wrong device name. Run
`groups` to confirm `dialout` is in your group list.

**`Qt6HttpServer not found`** — the package is split out from
`qt6-base-dev` on most distros. Make sure you installed
`qt6-httpserver-dev` (Debian/Ubuntu) or the equivalent.

**Browser shows "WebSocket Close" immediately** — the daemon isn't
running, or the WS port (`8888`) is firewalled. Check `ss -tlnp | grep
8888` on the host.

**Amp doesn't respond to button presses** — the FTDI cable might be in
DTR-RTS mode. Many SPE amps need flow control disabled. The daemon
opens the port without flow control by default; if you've manually
configured something, reset the amp and try again.

## Older Qt (Ubuntu 22.04)

Ubuntu 22.04 ships Qt 6.2, which is below the 6.4 floor the project
asks for. You have two choices:

1. **Use the official Qt online installer** at https://www.qt.io/download
   to install Qt 6.4+ side-by-side, then point CMake at it:
   ```bash
   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_PREFIX_PATH="$HOME/Qt/6.4.3/gcc_64"
   ```
2. **Upgrade to Ubuntu 24.04** — the easiest path long-term.

Lowering the Qt floor in `CMakeLists.txt` from 6.4 to 6.2 will compile
but is unsupported (we use a couple of 6.4-only `QHttpServer` APIs).

## License

GPL-3.0-or-later. See [`LICENSE`](../LICENSE) for the full text.
