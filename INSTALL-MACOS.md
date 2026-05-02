# Installing on macOS

Two paths, depending on whether you want a one-click run or the
ability to modify the source. The pre-built binaries cover both
**Apple Silicon (M1/M2/M3/M4)** and **Intel** Macs.

> **Tested on:** macOS 13 Ventura and later. Older releases may work
> but Qt 6.8 only officially supports macOS 12+.

## 1. Quick install — pre-built binary

Grab the right tarball for your Mac from
[the Releases page](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest):

| Mac | File |
|---|---|
| Apple Silicon (M1/M2/M3/M4) | `spe-remote-qt-<version>-macos-arm64.tar.gz` |
| Intel | `spe-remote-qt-<version>-macos-x64.tar.gz` (uploaded post-release once a build runner becomes available — see [§Building from source](#building-from-source) if it's not on the release yet) |

Not sure? Click the Apple menu → **About This Mac** — the **Chip**
line says either *Apple M1/M2/...* or *Intel Core i7/i9/...*.

Unpack and run from a terminal:

```bash
tar -xzf spe-remote-qt-*-macos-*.tar.gz
cd spe-remote-qt-*
./spe-remote-qt          # desktop GUI
# or for headless:
./spe-remoted --help
```

The first launch will be **blocked by Gatekeeper** ("Apple cannot
check this app for malicious software") because the binary isn't
notarized. Two ways past it:

* **Right-click → Open** the first time, then click **Open** in the
  warning dialog. macOS remembers the choice.
* Or from a terminal:
  ```bash
  xattr -dr com.apple.quarantine spe-remote-qt spe-remoted
  ```
  This strips the quarantine flag so you can launch normally.

## 2. Connect to the amp

You need a USB-to-serial cable connected to the amp's CAT port. SPE
amps ship with an FTDI-based USB cable; macOS picks it up
automatically without driver installs since 10.10.

Click the **⚙** at the bottom of the chassis to open Settings, pick
the port that looks like `/dev/cu.usbserial-AB0XXXXX` (FTDI ports get
a **★** marker), set baud to `115200`, click **Apply**. Connection
LED goes green.

The browser UI is automatically served at
[`http://localhost:8080/`](http://localhost:8080/) while the app is
running, so any phone or tablet on the same Wi-Fi can reach it via
`http://<your-mac-ip>:8080/`.

### Headless daemon

If you want a server-only setup (no GUI window), run `spe-remoted`
from a terminal. Useful if you're treating an old Mac mini as the
amp's network bridge:

```bash
./spe-remoted --list-ports        # see what serial ports exist
./spe-remoted --serial-port /dev/cu.usbserial-AB0XXXXX
./spe-remoted --help              # all flags
```

The daemon serves the web UI at the same `http://localhost:8080/`.

## 3. First-run notes

* **Gatekeeper warning** — see the `xattr` workaround above.
* **No FTDI driver needed** on macOS 10.10+. If `--list-ports` is
  empty, double-check the USB cable and try a different port; the
  built-in Apple driver handles FTDI silently.
* Saved settings live at `~/Library/Preferences/spe-remote/config.json`.
* The pre-built tarball runs against the system Qt frameworks bundled
  alongside it — no separate install needed.

## Updating to a newer release

Download the new tarball and unpack it on top of the old folder.
Your saved settings under `~/Library/Preferences/spe-remote/` are
untouched.

## Building from source

For modifying the code or running on a Mac older than what the
release binaries target. Install build deps via Homebrew:

```bash
brew install qt6 cmake ninja
git clone https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver.git
cd SPE-Expert-Amplifier-Remote-Webserver
git checkout v1.4.0       # or stay on main for the latest

cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)"
cmake --build build -j
```

The two binaries land in `build/spe-remote-qt` and `build/spe-remoted`.
Homebrew installs the right Qt build for your Mac's architecture
automatically (arm64 on Apple Silicon, x86_64 on Intel).

## Adding a password (optional)

If anyone else is on the network, gate the daemon behind HTTP Basic
auth. Generate a hash without writing the plaintext anywhere:

```bash
./spe-remoted --hash-password "your-password-here"
# pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP...
```

Add to `~/Library/Preferences/spe-remote/config.json`:

```json
{
  "auth_user": "operator",
  "auth_password_hash": "pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP..."
}
```

Restart the app. Same-LAN browsers skip the prompt automatically;
off-LAN clients need credentials. Browsers carry credentials onto
the WebSocket upgrade only if you put them in the URL —
`http://operator:your-password-here@host:8080/` for the first
connection.

For HTTPS / WSS, set `cert_file` + `key_file` in the same config —
see the project README's **"Securing the daemon"** section.
