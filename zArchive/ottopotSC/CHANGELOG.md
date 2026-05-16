# Changelog

## [1.1.0] - 2025-05-21

### Added

- Documentation in platform.io to help Linux users and people with multiple Teensys
- Debugging helpers to tune the deadzone (typically disabled)
- Ability to set the LED colors via MIDI CCs

### Changed

- Rewrote the handling of the endless potentiometers based on atan2 but adjusted to work linearly (thanks to [ensonic](https://github.com/ensonic/octacon/))
- Improved the deadzone handling allowing for slower movement

### Fixed

- Bitwig controller script: Fix automatic detection on Linux (which uses different MIDI port names)
