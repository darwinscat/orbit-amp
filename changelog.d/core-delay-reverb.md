### Fixed
- **The delay's repeats arrive exactly when TIME says.** After the time was changed, the echo settled a
  hair short of the new time and stayed there — at 48 kHz, 28 samples early at 1.5 s and 2.3 ms at a
  whole note at 40 BPM — so every repeat came that much early. It now lands on the time it is set to.
- **A reverb that has just been loaded no longer lets a flash of the dry guitar into its first 10 ms.**

### Changed
- **On an Intel Mac or PC the reverb plays exactly what it plays on Apple Silicon.** The two used to
  part by up to a millionth of full scale, some 120 dB down.

### Dependencies
- `felitronics-core` **v0.60.0 → v0.61.0** — the delay and the reverb now come from it, the same code
  the browser demo plays. With the knobs standing still the delay sounds as it did to within 2.4e-7
  and the reverb to within 6.5e-5 (MODULATED; the other five rooms within 3e-14).
