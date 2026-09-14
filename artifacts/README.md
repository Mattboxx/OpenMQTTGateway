# Pre-release reliability candidate — revision 5

These BLE and NO-BLE files contain the September 2026 reliability
changes and match the hashes in `SHA256SUMS.txt`. Each USB ZIP contains the flash
utility, guided Windows scripts, bootloader, partition table, OTA boot data and
firmware. Revision 5 is published separately as a pre-release; the preceding
public releases have not been replaced.

**Not validated as stable:** the preceding BLE revision stopped responding during a repeated
GPIO-page load test after 51 successful mixed HTTP requests. Investigation is
ongoing; recovery yielded no new saved crash. Revision 5 further hardens the
emergency reset path. Do not distribute these files as a final stable release.
The NO-BLE candidate compiled successfully but has not been installed in this
validation session.
