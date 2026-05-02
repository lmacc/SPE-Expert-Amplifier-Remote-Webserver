# Changelog

All notable changes to **SPE Expert Amplifier Remote Webserver** are
documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.4.0] — 2026-05-02

### Added
- **HTTP Basic auth** on every web UI and REST endpoint, and on the
  WebSocket upgrade. Off by default; enable by setting `auth_user`
  + `auth_password_hash` in `config.json` (or via `--auth-user` /
  `--auth-password` on the daemon CLI). Hashes use PBKDF2-SHA256
  with 120k iterations and a 16-byte random salt; the iteration
  count is stored in the hash so cost can be raised later without
  invalidating existing config.
- **LAN trust** — when auth is enabled, peers on a private network
  (RFC1918, loopback, link-local, IPv6 ULA) skip the password
  prompt by default. Disable with `trust_lan: false` in
  `config.json` or `--no-lan-trust` on the CLI. `/api/health` and
  `/api/config` report the live `trustLan` state.
- **`--hash-password`** CLI flag on `spe-remoted` — generates a
  hash for pasting into `config.json`, never persists plaintext.
- **Optional TLS** on both HTTP (now HTTPS) and WebSocket (now WSS).
  Set `cert_file` + `key_file` in `config.json` (PEM, RSA or EC).
  Falls back to plain HTTP/WS with a stderr line if cert/key load
  fails rather than refusing to start.
- **`/api/health`** endpoint — unauthenticated so monitoring tools
  keep working: returns `{ok, connected, secure, auth, trustLan}`.
- **`/api/config`** now also reports `{auth, secure, trustLan}` so
  the web UI can surface the security state.
- **"Created by EI5GJB"** credit in the top-right of both the Qt
  desktop chassis and the browser UI, sharing the brand strip's
  font.
- **macOS Intel binaries** in the release matrix — `macos-13`
  (Intel) joins `macos-14` (Apple Silicon), so every release tag
  produces five tarballs covering Windows x64, Linux x64, Linux
  ARM64, macOS arm64, and macOS x64.
- **GitHub Actions CI**: every push to `main` builds Windows x64,
  Linux x64, and Linux ARM64 so regressions are caught before
  tagging.
- **GitHub Actions release workflow**: pushing a `v*` tag builds
  binaries on all five platforms and attaches them to the GitHub
  Release automatically.
- **Screenshots** on the README landing page (Qt desktop app,
  browser UI, Connection settings dialog).
- `INSTALL-WINDOWS.md` — download / unzip / run walkthrough,
  SmartScreen and FTDI driver notes, build-from-source appendix.
- `INSTALL-MACOS.md` — covers Apple Silicon and Intel paths,
  Gatekeeper workaround, Homebrew build-from-source recipe.
- `CHANGELOG.md`, `CONTRIBUTING.md`, issue templates, PR template,
  README badges.

### Fixed
- `HttpServer::listen()` — `QAbstractHttpServer::bind()` returns
  `void` in Qt 6.5+, not `bool`. The `QTcpServer::listen()` check
  earlier in the function already handles the only real failure
  path; this was a latent build error against modern Qt.
- Cross-Qt-version compat on `QHttpServerResponse` headers and
  `QHttpServerRequest::headers()` so the same source builds against
  Qt 6.4 (Ubuntu apt) and Qt 6.7+ (Windows / macOS / fresh Linux).

### Documented
- New "Securing the daemon" section in the README — hash-password
  recipe, openssl self-signed cert recipe, deployment-scenario
  table mapping environments to auth/TLS combinations.
- Windows port-8080 / Hyper-V WinNAT collision in `INSTALL-WINDOWS.md`
  with the Connection-Settings escape hatch.

### Notes
- Browsers do not propagate the `Authorization` header onto
  WebSocket upgrades from JavaScript. With auth enabled, load the
  bundled web UI as `http://user:pass@host:port/` so the browser
  carries the credentials onto the WS connection. A real login flow
  in the web UI is on the roadmap for the next release.

## [1.3.0] — 2026-04-30

### Added
- **SWR bar graph on the LCD.** A third coloured bar (green ≤ 1.5,
  amber ≤ 2.0, red > 2.0) sits between PA-OUT and I-PA, sourced from
  `aswr` (post-ATU SWR). Both the Qt-painted desktop chassis and the
  in-browser web UI render identically.

### Removed
- The numeric `SWR` field from the bottom stats row — the bar reads
  faster at a glance when fold-back is brewing.

### Notes
- Wire format unchanged; `aswr` was already present in the status JSON
  since 1.0.0, this release just renders it as a bar.

## [1.2.1] — 2026-04-27

### Fixed
- DTR-pulse power-on no longer re-asserts after the pulse, so the amp
  stays on. Pulse width tuned to ~300 ms — longer was unreliable, much
  shorter was missed by the front panel.

### Added
- Upgrade docs in `INSTALL-LINUX.md` and `INSTALL-RPI.md` for users
  coming from 1.1.x.

## [1.2.0] — 2026-04-26

### Added
- Full programmer's-guide command set wired up. The chassis "menu /
  setup" strip now exposes ←, →, SET, DISP, L−, L+, C−, C+ in addition
  to the primary INPUT / ANT / TUNE / PWR / CAT / OP / OFF row.
- Five extra fields parsed from the 19-field status string:
  `memory_bank`, `atu_mode`, `rx_antenna`, `pa_temp_lwr`, `pa_temp_cmb`.

### Fixed
- Four opcodes that were off-by-one or transposed in the original
  reference. Verified against SPE Application Programmer's Guide
  Rev 1.1 §4.

## [1.1.0] — 2026-04-26

### Added
- **Power-on button** on the chassis. There is no documented opcode
  for power-on; the button pulses DTR for ~300 ms, which is the
  community workaround that wakes the amp from standby.
- `commandSent(opcode)` signal so the TX activity LED only pulses on
  user-initiated commands, not on background status polls.

## [1.0.0] — 2026-04-25

First public release.

### Added
- Native Qt6 / C++20 WebSocket bridge for SPE Expert linear amplifiers.
- **Two binaries, one codebase:**
  - `spe-remoted` — headless WebSocket + HTTP daemon. Runs on a Pi
    or any Linux/Windows box. Serial → WebSocket → browser UI.
  - `spe-remote-qt` — desktop GUI with the synthetic chassis, opens
    serial directly. Same web UI bundled in-process.
- Synthetic chassis renders crisp at any DPI; reflows on phones. Same
  7-button layout (INPUT/ANT/TUNE/PWR/CAT/OP/OFF) on Qt and web.
- Built-in HTTP server — no separate Apache / lighttpd needed.
- Settings page over HTTP for picking the COM port and baud rate at
  runtime; persisted to `config.json`. Supports first-run config from
  the browser.
- Auto-reconnect on serial errors (2 s backoff).
- Systemd unit and Raspberry Pi one-shot installer
  (`packaging/install-pi.sh`).
- Pre-built Windows x64 zip with all Qt 6 runtime DLLs bundled.

[Unreleased]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/compare/v1.4.0...HEAD
[1.4.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.4.0
[1.3.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.3.0
[1.2.1]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.2.1
[1.2.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.2.0
[1.1.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.1.0
[1.0.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.0.0
