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

## S2 — The client is told its own ports, 2026-10-06

`net::map_advertised_port` and `net::set_advertised_port_base` in `src/network.cpp`, used in
`src/nvhttp.cpp` (serverinfo's `HttpsPort`/`ExternalPort`, the launch and resume `sessionUrl0`) and
`src/rtsp.cpp` (SETUP's `server_port`); set from `/api/beam/pairing`'s optional `port`. Tests in
`tests/unit/test_network.cpp`.

Moonlight learns every port after the first from Sunshine's replies, and upstream replies with the
ports it binds. Beam's guest reaches Sunshine through a tunnel that listens on the *guest's*
machine, on a port base of the guest's choosing, so it must be told that base's ports instead --
otherwise a PC whose own Sunshine is on the same numbers cannot connect out without stopping it.

`/api/beam/pairing` takes `"port": <the guest's base>` with the approval, and until that session's
pairing is cancelled every advertised port is derived from it; Sunshine still binds its own. With
no `port` (or 0) nothing changes.

## S3 — Audio never goes silent for want of channels, 2026-10-06

`src/platform/windows/audio.cpp` (`mic_wasapi_t::init` and `_fill_buffer`), with the widening in
`src/platform/windows/beam_audio.h`; tests in `tests/unit/platform/windows/test_beam_audio.cpp`.

Upstream captures only in the stream's channel count. A guest asking for 5.1 or 7.1 from a host
whose output device is plain stereo, with no virtual surround sink to switch to, got
`Couldn't find supported format for audio` and no sound for the whole session -- seen on Beam as
a guest on 7.1 receiving zero audio packets from one laptop, depending on what that host's output
device was at the time. Now, if no format matches, the device is captured in stereo and each frame
widened to the stream's channels: front left and right carry the sound, the rest are silent. A
device that can do surround is captured exactly as before.

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
