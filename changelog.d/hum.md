Amps: OrbitAmp, CatpitAmp

### Added
- **HUM — mains hum taken off the input** (OrbitAmp and CatpitAmp). Before the input trim, the 50 or 60 Hz hum
  and its harmonics are cancelled by a comb of notches that follows the mains. The notches are 0.5 Hz wide while
  you play, so a note beside a harmonic is barely touched, and 3 Hz wide while the strings are quiet, so the hum
  is learned fast. The ring a note leaves in a notch is put back when the note stops, so a palm-muted chug no
  longer drags an after-ring behind it.
- **Off / 50 / 60 / Auto**, a machine setting shared by every window. Auto listens and decides 50 or 60 Hz by
  itself in the first quiet second, and its line says "listening" until then. On a machine that never chose,
  HUM starts Off and its first On is Auto. Off is a bypass: the stage keeps learning, and the way back is instant.
  It listens only to the inputs the chain plays, so a hissing unused input does not hold Auto back.
- **Where**: CatpitAmp has it in the INPUT lever's menu and in the settings menu's INPUT section, OrbitAmp in its
  input column. The **H** key flips HUM between Off and the last mode, and on the cockpit a tag says which.
- **LEARN** heads every HUM menu: mute the strings, press it, and the stage learns the hum in a couple of
  seconds and shows its verdict. The gate's LEARN stays the gate's own.
- Machine settings for tuning by ear: the notch width while playing (0.25–4 Hz), the width while quiet
  (1–6 Hz) and how high it reaches (1.2–6.4 kHz, 2 kHz by default).
- Known limits: a note held longer than 3 s with a partial on a mains harmonic can leave a short ring after it
  stops. The plain case is the A string on 50 Hz mains, 5 × 110 = 11 × 50. A soft attack right after a note can
  leave one too.

### Dependencies
- `felitronics-core` **v0.59.0 → v0.60.0**: the first release with `felitronics::dehum`, the hum canceller HUM
  is built on.
