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
whose output device cannot do it, with no virtual surround sink to switch to, gets
`Couldn't find supported format for audio` and no sound for the whole session. Now, if no format
matches, the device is captured in stereo and each frame widened to the stream's channels: front
left and right carry the sound, the rest are silent. A device that can do surround is captured
exactly as before.

A safety net, not a fix for something observed. It was written for guest sessions that received no
audio packets at all -- which turned out to be hosts playing nothing: Sunshine sends audio only
while the host makes a sound.

## S4 — Encoders are probed once, at startup, 2026-10-06

`video::probe_encoders` in `src/video.cpp`.

Upstream probes every encoder when Sunshine starts, then again on the first stream: Windows'
`needs_encoder_reenumeration` takes its snapshot of the GPUs only on that first call, and reports
"reenumeration is required" whenever it has none -- deliberately, for races while a system boots.
Beam starts Sunshine when Beam launches and keeps it running, so that second probe was paid by the
first session of every run: 0.43 s on one test PC, about 1.2 s on another.

The snapshot is now taken as part of the probe itself, so the first stream skips it unless the GPUs
actually changed since -- a GPU that appears, disappears or resets after startup still makes the
next stream probe again. Measured on the same PC, first launch after startup: 431 ms before, 18 ms
after.

## S5 — A guest's mode only ever raises the display, 2026-10-06

`display_device::keep_only_improvements` in `src/display_device.cpp`, applied in
`configure_display`; tests in `tests/unit/test_display_device.cpp`.

Upstream applies the client's mode as it is when display changes are allowed, so a 60 Hz guest
dropped a 144 Hz host to 60 Hz on every session -- over a second of `/launch`, a flicker on the
host's screen, and nothing gained, because Sunshine captures and scales either way. Now the
request is compared with the display as it is: a resolution is kept only if larger in width or
height, a refresh rate only if higher (half a hertz of slack, so 59.94 is 60), HDR only if it turns
it on. The rest stays as it is, and the log says so. Measured with a lower mode requested: launch
19 ms, no mode change.

## S6 — A session's guest is trusted by its certificate, 2026-10-06

`beam_trust_client` and `beam_server_cert` in `src/nvhttp.cpp`; the `clientCert` body in
`src/confighttp.cpp`.

Pairing swaps two certificates under a PIN, over four round trips. Beam already has a channel both
sides trust, its own signalling, so it swaps them there (Beam's backlog C5, beam-view's P12):
`POST /api/beam/pairing` takes `clientCert` (the guest's certificate, PEM) instead of `pin`, and
the certificate goes into the HTTPS server's trusted chain and the device list -- what a completed
pairing leaves behind. It opens the session (S7) and sets its ports (S2) as a PIN approval does,
and `/api/beam/pairing/cancel` removes the device again. Either way the response now carries
`serverCert`, this Sunshine's own certificate, for the guest to pin instead of learning it by
pairing.

Nothing secret crosses: both certificates are public, and each side still proves on every TLS
connection that it holds the matching private key. Checked live: the guest streams to a first frame
with no pairing; a guest pinning the wrong certificate is refused; after cancel it is refused and
no device is left.

## S7 — Nothing streams outside a Beam session, and nothing is announced, 2026-10-06

`refuse_outside_beam_session` in `src/nvhttp.cpp` (`launch`, `resume`), and the mDNS start in
`src/main.cpp`; test in `tests/unit/test_http_pairing.cpp`.

Upstream streams to any paired device whenever it asks. Beam clears pairings at both ends of every
session, but a crash or a bug that left one behind would let that device stream with nobody
pressing Allow. `/launch` and `/resume` are now refused -- 403, "No Beam session is open" -- unless
a Beam session is open: approved with `/api/beam/pairing` and not yet cancelled. Checked live with a
device still paired after its session was cancelled: refused.

And a Sunshine bound to loopback no longer announces itself over mDNS: nothing on the network could
connect to it, so announcing would only advertise the PC.

## S8 — The host switches the guest's input during a session, 2026-10-09

`beam_allow` in `src/input.cpp`, and `POST /api/beam/input` in `src/confighttp.cpp`.

Upstream's `mouse`, `keyboard` and `controller` settings already gate the guest's input on the
host, after the control stream is decrypted -- so a modified client cannot get around them -- but
they are read from the config file at start. Beam's host wants to hand over and take back control
mid-session. The route sets the three in memory, on the task pool that handles every input packet,
so a change lands between packets. A category turned off first releases whatever the guest holds
in it: mouse buttons, keys (and any key repeat), and each controller is set back to rest. Logged as
`Beam: guest input -- mouse on, keyboard off, controllers on`.

The config file keeps Beam's settings; Beam sets the live values again at the start and end of
every session, so a change never outlives the session it was made in.

## Test builds

Each patch is tried as a pre-release tagged `beam-dev` -- replaced every time, with
`Sunshine-Windows-AMD64-portable.zip.sha256` beside the zip -- before it goes into a numbered
release. Beam fetches one with `node scripts/fetch-engines.mjs --dev sunshine`.

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
