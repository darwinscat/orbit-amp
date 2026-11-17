Amps: OrbitBass, RedGrooveBass, DeepGrooveBass

### Fixed
- **BLEND's low end no longer dips.** BLEND's clean leg is now lined up with each capture. Every bass capture carries how
  late it answers its input and whether it answers it inverted, and when a pack loads each capture's clean leg is also
  matched to the way its low end turns, read off the capture itself in the background, a tenth of a second a capture.
  Before, the low end thinned between the clean leg and the capture: at 50/50 on the ReVolt Bass Dirt by up to 16 dB
  around 35 Hz, on the SGT-DI by 2.5 dB at 30 Hz, on the ReVolt Bass Drive by 2 dB. Now BLEND sums its low end within
  half a decibel from 30 to 260 Hz.

### Dependencies
- **felitronics-guitar-core v0.12.0** (from v0.8.0): the crossover blend's clean match. **namz v5.1.0** with it.
