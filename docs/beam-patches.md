# beam-share: Beam's patches to Sunshine

beam-share is [Beam](https://github.com/Beam2026/BEAM)'s GPL-3 fork of
[Sunshine](https://github.com/LizardByte/Sunshine). Beam runs it as the host engine, as a separate
process, and drives it over its HTTP API on `127.0.0.1`. The patches live on branch `beam`, which
starts from the upstream release Beam shipped before the fork (`v2026.516.143833`), so each one can
be rebased onto a newer upstream on its own. Every change is marked `Beam:` in a comment.

Releases are tagged `beam-vX.Y.Z` and publish `Sunshine-Windows-AMD64-portable.zip`, the same
asset upstream publishes, which Beam's `fetch-engines.mjs` pins by tag and SHA-256.

## S1 — Pairing is per session, 2026-10-06

`src/nvhttp.cpp` (`pair`, `beam_arm_pairing`, `beam_cancel_pairing`), `src/confighttp.cpp`
(`/api/beam/pairing`, `/api/beam/pairing/cancel`), tests in `tests/unit/test_http_pairing.cpp`.

Upstream's `POST /api/pin` names no client. Sunshine parks a pairing request (`getservercert`) until
a PIN arrives and applies the PIN to whichever request is parked, and Moonlight sends the same
`uniqueid` from every client, so nothing in a request says who it is from. For Beam that meant a
request left parked by a cancelled session took the *next* session's PIN, and a guest's retry
failed against its own earlier attempt as "Out of order call to getservercert". Beam spent weeks
working around both from outside.

beam-view (Beam's Moonlight fork, P10) now sends `beamid=<Beam session id>` on every pairing request.
With it:

- `POST /api/beam/pairing {"id", "pin", "name"}` approves one session's pairing. If that session's
  request is already parked it is answered now (`"answered": true`); otherwise the PIN waits, and the
  request is answered the moment it arrives instead of being parked. An approval nobody comes for
  expires after 60 s. Approving again for the same session replaces the PIN.
- `POST /api/beam/pairing/cancel {"id"}` forgets the approval and answers any request the session
  left parked with a refusal, so nothing outlives the session.
- A new `getservercert` for a session replaces that session's earlier one rather than failing.
- Sessions are kept apart in the pairing map (`beam:<id>`), and the map now has a lock: upstream
  reaches it from the HTTP and HTTPS server threads and from the web UI's thread without one.

A request with no `beamid` behaves exactly as upstream.

## Building on Windows

Upstream's `docs/building.md` applies, with MSYS2 UCRT64. Three things it leaves out, each of which
fails the configure step:

- **Run `pacman -Syu` first, really.** The prebuilt FFmpeg from `build-deps` links x264/x265 built
  against a newer MinGW runtime; on an older one the final link fails with `undefined reference to
  ftime64`.
- **A login shell loses `PROCESSOR_ARCHITECTURE`**, so CMake asks for `Windows--ffmpeg.tar.gz` and
  the download fails — and leaves a broken cached archive that later fails as "extraction failed".
  Export `PROCESSOR_ARCHITECTURE=AMD64` and delete `build/` after a failed download.
- **Native Node.js must be on `PATH`** inside the MSYS2 shell, and **WiX needs a .NET SDK.** Beam
  ships the portable zip, not the MSI, so `-DCMAKE_IGNORE_PATH="C:/Program Files/dotnet"` lets WiX
  skip itself.

A release build matches upstream's CI (`ci-windows.yml`): `-DSUNSHINE_ASSETS_DIR=assets`, then
`cpack -G ZIP` in `build/`.
