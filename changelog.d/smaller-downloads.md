Amps: all

### Changed
- **Smaller downloads.** Every amp is built with link-time optimisation and without its internal symbol
  names, and without the Ogg Vorbis, FLAC and Windows Media readers no amp ever used; the Mac package and the
  Windows installer are packed tighter.
- **A Mac zip for each kind of Mac**: `darwinscat-amps-<version>-macos-arm64.zip` for Apple silicon and
  `…-macos-x86_64.zip` for Intel, in place of one universal zip. The `.pkg` stays universal.
- **The Linux download is a `.tar.xz`**, `darwinscat-amps-<version>-linux-<arch>.tar.xz`: unpack it with
  `tar -xf`, as before.
