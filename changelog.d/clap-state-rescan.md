Amps: all

### Fixed
- **A session opened in a CLAP host shows its knobs where the session left them, at once.** Loading a
  session or a preset into an amp's CLAP now tells the host every parameter's value has changed, as CLAP
  asks; before, a host could go on showing the positions of before until the amp next played, and might
  take the new ones for automation.
