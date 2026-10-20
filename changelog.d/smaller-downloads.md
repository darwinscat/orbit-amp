Amps: all

### Changed
- **Smaller downloads.** Every amp is built with link-time optimisation and without its internal symbol
  names, and without the Ogg Vorbis, FLAC and Windows Media readers no amp ever used; the Mac package and the
  Windows installer are packed tighter.
- **The Mac download is the `.pkg` alone**, universal — for Apple silicon and Intel alike; no Mac zip any more.
- **The Linux download is a `.tar.xz`**, `darwinscat-amps-<version>-linux-<arch>.tar.xz`: unpack it with
  `tar -xf`, as before.
