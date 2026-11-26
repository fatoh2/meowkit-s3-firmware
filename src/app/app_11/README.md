# MeowGotchi for MeowKit-S3

Native ESP32-S3 app alongside NFC and the bonded-reconnect Air Mouse fixes.
Open MeowGotchi from the fourth row of the Apps grid. It opens paused with
PCAP capture and active testing **off** on every app launch.

## Controls

- **A** on the face: start/pause passive listening. **B**: menu.
- **Up/Down**, then **A**: choose a menu item or target. Rows and footer hints
  also accept taps. **Hold B**: exit through the launcher.
- **PCAP capture**: opt in to saving advertisements and complete EAPOL-Key
  frames to `/handshakes/meowgotchi-XXXXXXXX.pcap` on an already mounted SD.
  No SD is needed for discovery; a missing card or failed write is reported.
- **Target network**: select by SSID and BSSID. **Active test** then shows
  that BSSID and requires an additional A/tap to enable deauthentication.
  Use this only on networks you are authorized to test. It stays on the
  selected channel and sends at most one broadcast frame every 150 ms.
  Pause, exit, changing targets, or a transmit error turns active testing off.
- **Counters / errors**: EAPOL seen, candidate key pairs, frames/keys actually
  synced to disk, dropped frames, transmitted frames, driver errors and capture file.

Passive mode hops through the Wi-Fi driver's configured country channel range
every 400 ms. Start temporarily disconnects station Wi-Fi; pause/exit restores
the previous mode, power-save setting and automatic-reconnect preference, and
resumes a previously connected/connecting station. Settings scans and AP mode
are reported busy. Capture stays enabled across pauses, but each resumed
capture uses a new PCAP with fresh advertisements. Settings credentials are
never erased or rewritten by this app. Restoration errors are reported and
retained for retry. The shared SD helper mounts late-inserted cards without
formatting, adopts healthy mounts, and refuses filesystem access during USB
mass-storage mode. Enabling capture rechecks storage, so an SD error can be
retried after correcting the card without resetting the session counters.

## Limits and validation

This is a small 2.4 GHz auditing app, not the Linux/bettercap/AI Pwnagotchi stack.
It does not recover passwords or verify complete handshakes. A candidate pair
requires compatible M1/M2 or M2/M3 replay counters for the same AP and station;
the pair counter counts each tracked AP/station link once. PCAP records raw
802.11 without FCS (link type 105); timestamps are elapsed microseconds since
boot, not wall-clock time. Only complete frames up to 2304 bytes are saved.
Discovery is bounded to 96 APs, 128 stations and 32 candidate pair trackers.
Frames arriving faster than SD writes can drain the 16-frame queue are counted
as dropped. The saved count advances after a checked filesystem sync.
Capture buffers are synced each second and closed on pause/exit;
power loss may lose buffered data. Driver checks/errors remain visible.
SD status reflects the last storage check and any subsequent write/sync
failure. Errors include the failed operation and filesystem error number.
The writer uses open/write/fsync/close directly: the pinned FAT implementation
does not support the fcntl call required by Newlib's fdopen.

The active-mode SDK hook is pinned to ESP-IDF 4.4.3. Only the exact frame buffer
submitted synchronously by the owning app task for the selected BSSID bypasses
raw-frame validation; all other calls retain the SDK's original validation.
The build patches only two call-site relocations in a local copy of the
checksum-pinned SDK archive, preserving the original validator and shared SDK.
The post-link check verifies the actual call target. Review this ABI and the
linked call site before changing ESP-IDF.

Host parser tests (Linux, g++):

```sh
bash tools/tests/run_meowgotchi.sh
```

Tests cover malformed frames, QoS/HT offsets, replay/direction classification,
radio ownership/restoration/retry, capture resume, queue drops, actual file
open failures and a kernel-rejected buffered flush. Radio APIs are simulated
in a single task; filesystem operations are real temporary Linux files.
Firmware build: `pio run -e esp32s3box`. Compilation and host tests do not
establish device stability, RF reception, SD behavior, or active TX success;
those require a separate hardware test. This feature build is not flashed
automatically.

## Attribution

Cat drawing primitives in `cat_ui.h` and the launcher icon
`src/ui/images/ui_img_ic_meowgotchi.c` come from
[FeralDevs/FeralCat](https://github.com/FeralDevs/FeralCat/tree/1cf9fc9dd772b4acb42f9c2e4625722574a588a5),
revision `1cf9fc9dd772b4acb42f9c2e4625722574a588a5`:
`src/app/app_10/meowgotchi_ui.h` and `src/ui/images/ui_img_ic_meowgotchi.c`.
The MeowGotchi concept/UI informed this port. The frame parser, bounded capture
engine, radio lifecycle and app adapter were written for this firmware.
