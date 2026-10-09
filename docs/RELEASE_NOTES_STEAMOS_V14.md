# RPG Maker Player SteamOS 1.4 update

- Automatically prepares an empty _compat/patches.json under the selected game-library root, including after switching folders; existing profiles are never overwritten.
- MV/MZ and RGSS patch loaders resolve central rules from the selected library (with legacy installation-root fallback).
- Removes unverified Abaddon/example profiles from the default template; retains confirmed shared compatibility infrastructure and 1.3 base.
- SteamOS actual-device acceptance is pending; package is an incremental update over the previous release, not a standalone runtime.
