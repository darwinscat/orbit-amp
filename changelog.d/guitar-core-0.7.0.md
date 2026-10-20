Amps: all

### Changed
- **A capture's sound fades in once both captures the gain dial blends are ready** — at the amp's first sound and at
  every pack chosen. It used to start rising while one of the two was still on its way, and that one let the dry
  guitar through; now the four blocks it took (some 40 ms at 48 kHz in blocks of 512) are silent and the fade starts
  after them. From there on the sound is the same, sample for sample.

### Dependencies
- **felitronics-guitar-core v0.7.0, with namz v4.4.0**: a capture loads in a fifteenth of the time on a mono input
  and an eighth on a stereo one — it is built straight from its `.namz` instead of through a text copy of its weights,
  a mono input gets one network instead of two, and the silence it was warmed on before is gone, since the playing
  warms it before it is heard. A turn of the gain dial waits less for the capture it lands on, and a capture held for
  a mono input takes half the memory.
