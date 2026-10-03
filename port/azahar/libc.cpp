// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: what Azahar's core calls that the PS5's libc does not export to a title, built as
// FreeBSD's libc builds it.

#include <sys/ioctl.h>
#include <sys/sockio.h>

// The 3DS's sockets (Azahar's SOC:U service): whether a socket's next byte is out-of-band data.
extern "C" int sockatmark(int socket)
{
	int atMark = 0;
	if (ioctl(socket, SIOCATMARK, &atMark) == -1)
		return -1;
	return atMark;
}
