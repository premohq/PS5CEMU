# Building Releases

1) Change into the source directory.

2) Execute `./build deps` to install the required build dependencies and build the program. This supports the apt and dnf package managers on Linux. On Mac OS, MacPorts is supported.

3) Execute `./build` to generate a Linux deb file, Linux RPM file, and a portable zip release for Mac OS and Linux. You should've at least ran `./build deps` once before you use this.

If you want to clean the built releases, execute `./build clean`.

