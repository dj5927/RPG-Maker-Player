# RPG Maker Player SteamOS v1.7.1

## Update confirmation UI
- Moved the install Yes/No buttons into a centered dialog rather than
  drawing beyond the bottom of the 286px settings content box.
- Button hit boxes match their rendered positions for mouse/touch/Steam Input.

## OTA progress reporting
- Install helper writes atomic status updates into the local update cache.
- A responsive launcher displays a progress bar and percentage across:
  download (actual transferred bytes / HTTP Content-Length), checksum fetch,
  SHA-256 validation, archive safety checks, extraction and installation.
- Downloaded and total MB are displayed during transfer.
- The modal prevents accidental game launches or menu changes while updating.
- On success it displays 100% and asks for a launcher restart; failures
  report the specific error code.

## Safety and compatibility
- File replacement uses temporary files and atomic rename per payload file.
- Rejects malicious archive traversal/unsupported file types and does not
  overwrite user games, saves, launcher settings or custom compatibility DB.
- Keeps the existing Steam Stop/Exit fix and all v1.7 runtime updates.
- Proton download/selection behavior remains unchanged.
- Includes a cumulative OTA suitable for v1.3 and later SteamOS installs.
- NOTE: users updating *from* v1.7 or earlier see their old updater UI
  during the transition; after the required restart, future OTA updates
  show the new live progress bar.

## Verification
- Linux SDL2 launcher build and modal containment smoke test.
- Throttled HTTP OTA test verifies percentage updates through download
  to 100%, a SHA-256 mismatch rejection and traversal attack rejection.
- Steam exit regression and user data preservation checks.
- SteamOS Gamescope device acceptance remains to be tested.
