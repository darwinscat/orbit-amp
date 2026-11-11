Amps: CatpitAmp, RedPawAmp, FrostPawAmp, AmberPawAmp

### Fixed
- **Opening the face writes no automation.** Every time the face opened, it set the amp's chain up again and told the
  host of each parameter it set, even one already where it stood, so a host writing automation recorded points
  nobody made. Now the host hears only of a parameter that moves.
