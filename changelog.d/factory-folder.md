Amps: all

### Changed
- **The amps' data is installed once, in one folder they share.** The packs, the cabinets and the faces'
  pictures were built into every amp, in every format, for both Mac architectures — the same 26 MB many times
  over. Now every installer lays them out once, as a part of it that cannot be unticked: in
  `/Library/Application Support/Darwin's Cat/Factory` on a Mac, `C:\ProgramData\Darwin's Cat\Factory` on
  Windows, and `~/.local/share/darwinscat-amps/Factory` on Linux (`/usr/local/share/…` with `--system`).
  Every amp plays what is its own in it, and the download is a third of what it was. The zips carry the same
  folder, with a README that says where to copy it.
- An amp that cannot find that folder says so, and where it belongs, instead of opening an empty face.

### Removed
- **OrbitAmp's OLD**, the Victory V4's six packs, is no longer shipped.
