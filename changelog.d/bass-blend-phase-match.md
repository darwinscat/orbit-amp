Amps: OrbitBass, RedGrooveBass, DeepGrooveBass

### Fixed
- **BLEND's low end no longer dips.** A capture's lag and polarity line BLEND's clean leg up with it only where the
  capture's phase is a delay, and a bass pedal's own coupling filters turn its low end ahead of that: between the clean
  leg and the capture the low end thinned — at 50/50 on the ReVolt Bass Dirt by up to 16 dB around 35 Hz, on the
  SGT-DI by 2.5 dB at 30 Hz, on the ReVolt Bass Drive by 2 dB around 60 Hz. Now every capture's clean leg is matched to
  its phase when a pack loads — read off the capture itself, in the background, a tenth of a second a capture — and
  BLEND sums its low end within half a decibel from 30 to 260 Hz. Nothing in the packs changed.

### Dependencies
- felitronics-guitar-core with the crossover blend's clean match (measureCleanMatch).
