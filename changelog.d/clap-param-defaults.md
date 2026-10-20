Amps: all

### Fixed
- **A host is told the defaults an amp really opens on, and an amp it has just opened keeps its knobs still.**
  Each parameter's default, as a host reads it, is now where a new instance stands: CatpitAmp's and the
  rovers' room, room mix, delay and amp the ones their first channel opens on, and STEREO MODE the one the
  amp picks for where it runs — STEREO on a stereo track, MONO on a mono one, STEREO SPACE in the
  standalone. The mode is picked as soon as the host settles the track's channels, no longer when playback
  starts, so a host no longer sees the amp move a parameter by itself as it starts playing. How an amp
  opens and what it plays are as before, and so is everything the amp puts back itself — INIT, Reset to
  default, a double-click on a control; only a host's own "reset to default" goes where the amp opens.
