# Contributing

Thanks for taking a look — this is a small but real project and
help is welcome.

## Reporting bugs

Open an
[issue](https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver/issues/new/choose)
using the **Bug report** template. The most useful things to include:

- The amp model + firmware version (visible on the front panel
  display in setup mode).
- Your platform: Windows / Linux / Pi / macOS, plus distro version.
- The build version: `spe-remoted --version` or `spe-remote-qt --version`.
- The serial device — manufacturer/VID:PID from `--list-ports` if you
  can.
- Logs: `journalctl -u spe-remoted -n 200` on a Pi, or the daemon's
  stderr if running it interactively. Include the JSON line that
  arrived from the amp if a status frame is involved.

## Asking for a feature

Use the **Feature request** issue template. If it's something
specific to your amp model and you can capture a serial trace, that
makes implementation a lot more concrete than guessing from the
programmer's guide.

## Sending a PR

1. Fork the repo, branch from `main`.
2. Keep changes focused — one logical change per PR. Mixed-purpose
   PRs are hard to review.
3. Match the existing style:
   - C++20, snake_case for files, `lowerCamelCase` for methods.
   - No new third-party dependencies without discussion — Qt6 modules
     are fine, anything else needs justification.
   - Web UI is plain HTML / CSS / JS (no framework). Keep it that way.
4. The CI on `main` builds on Windows, Linux x64, and Linux ARM64. PRs
   run the same checks.
5. Update [`CHANGELOG.md`](CHANGELOG.md) under the `## [Unreleased]`
   section.

## Touching the amplifier protocol

If you're changing anything in `src/AmpController.{h,cpp}` or the
status-string parser:

- Cite the section of the SPE Application Programmer's Guide you're
  working from in the commit message — opcode tables in the PDF are
  ambiguous in places and a citation makes review tractable.
- If you've verified an opcode against a live amp, say so. "Verified
  on a 1.3K-FA, FW 4.21" is gold.
- Don't change wire-format JSON field names without flagging it
  loudly — older browser clients on the LAN may still be running off
  cached HTML.

## Testing locally

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/spe-remoted --list-ports
./build/spe-remote-qt
```

The desktop GUI is the easiest way to exercise a change end-to-end —
press buttons, watch the LCD update from real status frames.

## Release process (maintainers)

1. Bump `VERSION` in `CMakeLists.txt`.
2. Move the `## [Unreleased]` section in `CHANGELOG.md` under a new
   `## [x.y.z] — YYYY-MM-DD` heading; update the link references at
   the bottom of the file.
3. Commit, tag: `git tag -a vX.Y.Z -m "vX.Y.Z" && git push --tags`.
4. The `Release` workflow builds Windows / Linux / ARM64 binaries
   automatically and attaches them to the GitHub Release.
