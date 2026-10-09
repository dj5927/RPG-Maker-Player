# RPG Maker Player Android RC6 (A165)

- Restores the four previously verified, content-hash-matched XP compatibility patches for The Curse of Pleasure. This resolves the regression where Audio Extra (fixed) attempted File.join with nil.
- Automatically repairs A164 empty _compat/patches.json on the next library scan, with a backup and without overriding any custom profiles or explicitly disabled rules.
- The unverified Abaddon/test-game profiles remain excluded. Other game code, saves and Android engine behavior are unchanged from RC5.

Ruby 1.8/1.9/3.x exact archive tests and Gradle build passed; Android real-device validation remains pending.
