# Changelog

## Unreleased

Changes:

* Windows support: `ps5mfr` is now a Python script (instead of bash), with a `ps5mfr.bat` launcher for Windows. Handles paths longer than Windows' 260 character limit.

* The file counts only include files that were actually fake signed (failures were counted before), and failures are reported.

* Files that are not ELF files are skipped before being copied to the temp directory, instead of copying large game data `.bin` files there only for `make_fself.py` to reject them.

* If `make_fself.py` fails partway through writing a file, the original file is restored.

* No longer fake signs the plain copies in ps5 app dumper's `decrypted` backup folder, and restores the ones earlier versions did (checked against the SHA-256 of the original that `make_fself.py` stores in each file).

* Says when files are skipped because they're already fake signed or still encrypted, including `eboot.bin`.

* Writes a log of every run to `logs/`, with more detail than the screen, and the error if ps5mfr crashes.

----------------------------------------------------

## Version 1.0 (6/2/2026)

Changes:

* Initial release.

----------------------------------------------------

* [ps5mfr-v1.0.zip](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0/ps5mfr-v1.0.zip) _Portable Zip file for Linux_

* [ps5mfr-v1.0-1.noarch.rpm](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0/ps5mfr-v1.0-1.noarch.rpm) _RPM package file for Linux._

* [ps5mfr-v1.0.deb](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0/ps5mfr-v1.0.deb) _Deb package file for Linux._

---------------------------------------