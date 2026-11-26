# Save a working universal signal

1. Open Universal Remotes, choose the device category and action, and scan.
2. When the device responds, press **A** to pause. The paused screen selects
   the last transmitted code; if a transmission is in flight, it selects that
   current code. **Left/Right** selects nearby codes and **A** tests one again.
3. Press **Down** while paused to open **Save Working Signal**.
4. Choose **Create new remote**, name it (for example `TV`), and select Save.
   Or choose **Add to saved remote**, browse to an existing `.ir` file, and
   name the new button. Duplicate button names are rejected; existing buttons
   and existing files are preserved.
5. A successful save opens the remote immediately. Later, open
   **Saved Remotes → remote/ → TV.ir**, select the button and press A to send.

New remotes go to `/infrared/remote/<name>.ir` on the SD card, using the same
`IR signals file` / `Version: 1` format as learned remotes. The file contains
the full decoded or raw signal, so using it does not depend on rescanning or
on a universal library retaining the same record numbers. **Up** resumes a
paused scan and **B** goes back. The final library signal remains paused so
it can also be tested and saved.

Universal libraries can run from bundled firmware without SD, but saving
requires a writable SD card. A failed save stays in the naming flow for retry
and uses the existing checked, transactional remote writer.

Validation: `bash tools/tests/run_ir_save.sh` checks pause selection, final-code
handling, exact signal copying, append preservation, duplicate/invalid/full
rejection, raw timings and normal saved-file reload using the actual codecs.
`bash tools/tests/run_storage.sh` checks storage and bundled libraries.
