# Installing on a Raspberry Pi

This is the deployment most people will run: a Pi sitting next to the
amplifier, headless, exposing the web UI to the rest of the LAN. The
Pi runs **only the daemon** — there's no display, no keyboard, no GUI
to launch.

## What you get

- `spe-remoted` running under systemd, started automatically on boot.
- Web UI reachable at `http://<pi-ip-or-hostname>:8080/` from any phone,
  tablet, or laptop on the same network.
- WebSocket endpoint at `ws://<pi-ip>:8888/ws` — the same one the old
  ESP8266 device used, so existing devices keep working.
- Persistent config at `/var/lib/spe-remote/spe-remote/config.json`
  (port name, baud rate) — survives reboots and reinstalls.

## Hardware expectations

- **Raspberry Pi 3, 4, 5, or Zero 2 W.** Pi Zero (original, ARMv6)
  won't work — Qt 6 needs ARMv7+.
- **Raspberry Pi OS Bookworm or newer.** Bullseye ships Qt 5 only;
  upgrade or reflash.
- **USB-to-serial cable** between the Pi and the amp's accessory port
  (the SPE Expert factory cable uses an FTDI chip — shows up as
  `/dev/ttyUSB0`).
- **Wired ethernet recommended** — a stable LAN connection matters more
  than CPU. Wi-Fi works, but a cable is what you want at the radio
  bench.

## Expectations / non-expectations

- **No authentication** is implemented. The Pi must live on a network
  you control. Don't port-forward `:8080` or `:8888` to the public
  internet.
- **No HTTPS.** Everything is plain HTTP / WS. If you need it offsite,
  put the Pi on a Tailscale tailnet or a Cloudflare Tunnel — those give
  you both transport security and an identity check.
- **Single client at a time** is the design centre. Multiple browsers
  can connect, but they all see the same amp state — there's no
  per-user view.
- **The Pi only runs the daemon.** If you want a desktop GUI, run
  `spe-remote-qt` from a Linux/Windows/Mac machine somewhere on the
  same LAN — see [`INSTALL-LINUX.md`](INSTALL-LINUX.md).

## One-shot install (recommended)

There's a wrapper script that does everything: installs Qt build deps,
compiles the daemon, drops the binary at `/usr/local/bin/spe-remoted`,
adds your user to `dialout`, installs the systemd unit, and starts the
service.

```bash
git clone https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver.git
cd SPE-Expert-Amplifier-Remote-Webserver/qt6-port
git checkout v1.4.0
sudo bash packaging/install-pi.sh
```

When it finishes you'll see the Pi's IP address and the URLs to point
your browser at.

The build takes 3–5 minutes on a Pi 4 / Pi 5, longer on a Pi 3 or
Zero 2 W (10–15 min, but it'll get there).

## Verify it's running

```bash
systemctl status spe-remoted          # active (running) — green
journalctl -u spe-remoted -f          # live logs
ss -tlnp | grep -E '(8080|8888)'      # both ports should be listening
```

Open `http://<pi-ip>:8080/` from any device on the LAN. The chassis UI
loads. Click the **⚙ cog** at the bottom of the panel, pick
`/dev/ttyUSB0` (or whatever the amp showed up as), set baud to
**115200**, click **Connect**. The connection LED at the bottom-left of
the chassis goes green and the LCD starts updating with live amp
telemetry.

## Reconfiguring later

You don't need to SSH in. The web UI's settings dialog (⚙) writes the
new port/baud to `config.json` and the daemon reopens immediately.

If you do want to change ports / behaviour at the systemd level —
say to pin a different USB device or change the listen ports — edit
the unit:

```bash
sudo systemctl edit --full spe-remoted.service
sudo systemctl restart spe-remoted
```

The relevant knobs are documented inline in the file. The unit also
allows access to `/dev/ttyUSB0`, `/dev/ttyUSB1`, `/dev/ttyAMA0`, and
`/dev/serial0` out of the box.

## Adding a password (optional)

If more than one person is on the network, gate the daemon behind
HTTP Basic auth. Generate a hash without writing the plaintext to
disk:

```bash
spe-remoted --hash-password "your-password-here"
# pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP...
```

Edit `/var/lib/spe-remote/config.json`:

```json
{
  "auth_user": "operator",
  "auth_password_hash": "pbkdf2-sha256$120000$Tk5Db21wVmQ...$qZ4AZP..."
}
```

`sudo systemctl restart spe-remoted`. To get into the web UI from a
browser the first time, paste credentials into the URL so the
WebSocket upgrade carries them along:
`http://operator:your-password-here@spe-remote.local:8080/`.

For TLS / wss, generate a self-signed cert (or drop in a Let's
Encrypt one) and set `cert_file` + `key_file` in the same config —
see the project README's "Securing the daemon" section.

## Updating to a newer release

`install-pi.sh` is idempotent — re-running it on a freshly checked-out
tag rebuilds the binary, replaces `/usr/local/bin/spe-remoted`, and
restarts the systemd service. The current release tag is shown on
[the Releases page](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/latest);
substitute it for `v1.4.0` below.

```bash
cd ~/SPE-Expert-Amplifier-Remote-Webserver           # wherever you cloned it
git fetch --tags
git checkout v1.4.0                  # or whatever the new tag is
sudo bash qt6-port/packaging/install-pi.sh
```

Your saved config (`/var/lib/spe-remote/spe-remote/config.json` — port
name, baud) is preserved, so the daemon comes back up on the same
serial device automatically.

**Verify the upgrade landed:**

```bash
systemctl status spe-remoted          # should be "active (running)"
spe-remoted --version                 # prints the new version
journalctl -u spe-remoted -n 30       # check the boot log
```

**Then hard-refresh the browser** (Ctrl-Shift-R, or Cmd-Shift-R on
Mac) so it drops the old cached HTML/CSS — every release re-bundles the
web UI into the daemon binary, so a normal refresh might still show
the previous UI.

**If `git checkout` complains about local changes**, you've probably
hand-edited the systemd unit or the install script in-place. Park them
out of the way and try again:

```bash
cd ~/SPE-Expert-Amplifier-Remote-Webserver
git stash                             # park local edits
git fetch --tags && git checkout v1.4.0
sudo bash qt6-port/packaging/install-pi.sh
git stash pop                         # only if you actually wanted those edits back
```

## Uninstalling

```bash
sudo systemctl disable --now spe-remoted.service
sudo rm /etc/systemd/system/spe-remoted.service
sudo rm /usr/local/bin/spe-remoted
sudo rm -rf /var/lib/spe-remote
sudo systemctl daemon-reload
```

## Troubleshooting

**`spe-remoted.service: failed to start` after a reboot.** USB enumeration
on the Pi is racy — the service can come up before `/dev/ttyUSB0`
appears. The daemon will retry every 2s on its own; if the first attempt
in `journalctl` shows "no such device" it's harmless. If it never
recovers, run `dmesg | tail -20` to see whether the FTDI driver
attached.

**`Permission denied` on `/dev/ttyUSB0`.** The unit runs as the user
who invoked `install-pi.sh` (default `pi`) and assumes that user is in
the `dialout` group. The script adds you, but the change needs the next
login. A reboot guarantees it.

**Web UI loads but shows "WebSocket Close".** The daemon is running but
binding failed on `:8888`. Something else on the Pi is holding the port
— check with `ss -tlnp | grep 8888` and stop the offender, or change
`SPE_WS_PORT=` in the systemd unit.

**Amp says "remote control disabled" / no telemetry.** The amp's CAT
mode needs to be enabled at the front panel for the serial protocol to
respond. On the SPE Expert front panel: Menu → CAT → On.

**The chassis UI looks broken on an old browser.** The web UI relies on
modern flexbox + grid; anything from the last ~5 years works. iOS 12
Safari and earlier do not.

## License

GPL-3.0-or-later. See [`LICENSE`](../LICENSE).
