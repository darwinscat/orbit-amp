### Fixed
- **The delay no longer reads past the end of its own memory.** On an Apple Silicon Mac it did so at its
  default settings, 31 times a second, whether it was switched on or only in the rig; on any machine
  some offset and time settings did the same. Whatever it found there was added to the sound. Usually
  that was silence. When it was not, it was a click or a burst of noise, and the limiter could pull the
  level down for a moment after it.
