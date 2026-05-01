# Changelog

All notable changes to **SPE Expert Amplifier Remote Webserver** are
documented here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- GitHub Actions CI: every push to `main` builds Windows x64, Linux
  x64, and Linux ARM64 (Pi) so regressions are caught before tagging.
- GitHub Actions release workflow: pushing a `v*` tag builds binaries
  on all three platforms and attaches them to the GitHub Release.
- `INSTALL-WINDOWS.md` — download / unzip / run walkthrough,
  SmartScreen and FTDI driver notes, build-from-source appendix.
- `CHANGELOG.md` — this file.

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

[Unreleased]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.3.0
[1.2.1]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.2.1
[1.2.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.2.0
[1.1.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.1.0
[1.0.0]: https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/releases/tag/v1.0.0
