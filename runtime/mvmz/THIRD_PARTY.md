# MV/MZ Linux compatibility components

This runtime adopts the Linux launch approach used by:

- `bakustarver/rpgmakermlinux-cicpoffs`
- `adlerosn/cicpoffs`

Included runtime components:

- `lib/cicpoffs`: case-insensitive FUSE view of the RPG Maker game directory.
- `lib/libulockmgr.so.1`: runtime dependency shipped by rpgmakermlinux-cicpoffs.
- `jspatches/case-insensitive-nw.js`: NW.js/Node/browser case-insensitive path fallback from rpgmakermlinux-cicpoffs/Kawariki integration.

The full reference repository is kept under `_dev/vendor/rpgmakermlinux-cicpoffs` for development traceability.
`cicpoffs` is GPL-2.0; source is available at https://github.com/adlerosn/cicpoffs .

