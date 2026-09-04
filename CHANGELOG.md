# Changelog

#### Table of Contents
- [Unreleased](#unreleased)
- [1.2.0](#120---2019-09-11)
- [1.1.0](#110---2018-11-07)
- [1.0.0](#100---2018-11-06)

___

## [Unreleased]
### Added
- Presence type option (`Playing` / `Listening` / `Watching` / `Competing`) on the `Main` preferences tab. `Listening` is used by default, so that Discord shows "Listening to ..." with a music note instead of "Playing ...".
- Uploaded artwork urls are now automatically converted into Discord media-proxy asset keys (`mp:external/...`), so that the artwork is actually displayed instead of the `?` placeholder.
- x64 build support: the component is now shipped as a multiarch (Win32 + x64) package.

### Changed
- Default text fields: the first line is now the track title, the second line is the artist.

### Fixed
- Fixed presence data being modified from worker threads while the artwork upload was in progress.

## [1.2.0][] - 2019-09-11
### Added
- Added playback status images.
- Added new options to `main` Preferences tab:
  - Playback status image: light, dark, disabled.
  - Disable Rich Presence when playback is paused.
  - Swap `paused` and `playing` images.
- Added `advanced` Preferences tab with options to customize component:
  - Discord application key.
  - Resource IDs for corresponding images in the component.
- Added a link to the title formatting help in `main` Preferences tab.

### Changed
- Improved the frequency of presence updates.

### Fixed
- Fixed title formatting not updating when pausing and resuming playback.
- Fixed one-character text not displaying.

## [1.1.0][] - 2018-11-07
### Added
- Added Preferences page with the following settings:
  - Text fields configuration via title formatting queries.
  - Track duration: elapsed, remaining, disabled.
  - Foobar2000 image: light, dark, disabled.
- Added main menu command to toggle component.

### Fixed
- Fixed some bugs with persistent track info.

## [1.0.0][] - 2018-11-06
Initial release.

[unreleased]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/TheQwertiest/foo_discord_rich/commits/v1.0.0
