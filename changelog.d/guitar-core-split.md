### Fixed
- **A captured device no longer replays a moment of the last sound when the host restarts the audio.**
  Changing the buffer size or the sample rate — and, in some hosts, pressing play — prepares the plugin
  again, and a capture could answer the first silence after that with a scrap of what it was playing
  before. It now starts clean.
- **After a sample-rate change, moving a dial across a pack's captures crossfades to the next one when it
  is ready — not before, and not long after.** The wait was still counted at the old rate: half as long
  as the capture needs after going from 48 to 96 kHz, four times too long after going from 192 down to 48.
- **With PACK LEVEL COMP off, restarting the audio no longer shifts a device's level for a moment.** For
  about 40 ms it played at the pack's own input trim — the one the switch turns off — and then crept back.

### Changed
- The version window lists `felitronics-guitar-core`, where the capture player now comes from, beside the
  other libraries the build was made from.

### Dependencies
- `felitronics-core` **v0.31.0 → v0.53.0.** The capture player — the NAM engine and the pack player — moved
  out of it into its own library.
- `felitronics-guitar-core` **v0.1.0**, new: that library.
