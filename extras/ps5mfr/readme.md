# PS5 Make FSELF Recursive

_By Alex Free_.

Runs [make_fself.py](https://github.com/ps5-payload-dev/sdk/blob/master/samples/install_app/make_fself.py) recursively on a given decrypted game dump folder (such as from [ps5 app dumper](https://github.com/EchoStretch/ps5-app-dumper)). This allows said dump to run on a Jailbroken PS5, given the game can be ran as a dumped folder with something like [shadowmountplus](https://github.com/drakmor/ShadowMountPlus).

| [Homepage](https://alex-free.github.io/ps5-make-fself-recursive) | [Github](https://github.com/alex-free/ps5-make-fself-recursive) |

## Table Of Contents

* [Downloads](#downloads)
* [Usage](#usage)
* [Notes](#notes)
* [License](license.md)
* [Building](build.md)

## Downloads

### Version 1.0.1 (6/2/2026)

Changes:

* Added detection for multiple `.bin`, `.elf`, and `.sprx` files.

----------------------------------------------------

* [ps5mfr-v1.0.1.zip](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0.1/ps5mfr-v1.0.1.zip) _Portable Zip file for Linux_

* [ps5mfr-v1.0.1-1.noarch.rpm](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0.1/ps5mfr-v1.0.1-1.noarch.rpm) _RPM package file for Linux._

* [ps5mfr-v1.0.1.deb](https://github.com/alex-free/ps5-make-fself-recursive/releases/download/v1.0.1/ps5mfr-v1.0.1.deb) _Deb package file for Linux._

---------------------------------------

[Changelog](changelog.md)

## Usage

Linux and Mac OS: `./ps5mfr <path to decrypted game dump>`

Windows: `ps5mfr.bat <path to decrypted game dump>`, or drag and drop the game dump folder onto `ps5mfr.bat`. Install [Python](https://www.python.org/downloads/) first.

## Notes

* Finds any files with `.prx`, `.sprx`, `.bin`, or `.elf` recursively.

* Every run writes a log to the `logs` folder next to ps5mfr (or `ps5mfr-logs` in your home folder if that one isn't writable), its location is shown at the end. It has more detail than the screen (like `make_fself.py`'s output for every file), so include it when reporting a problem.

* Requires Python 3.6 or newer, but contains it's own copy of `make_fself.py`.

* Files that are not ELF files are left alone, so running it again on an already fake signed dump is harmless. It tells you about files that are already fake signed, or still encrypted.

* Leaves the `decrypted` folder alone, which [ps5 app dumper](https://github.com/EchoStretch/ps5-app-dumper) keeps plain copies of the executables in (with its `enable_elf2fself` or `enable_backport` options on). Files in there that an earlier version of ps5mfr fake signed are restored, but only when the restored file is verified to be exactly the original.
