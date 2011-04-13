/*-
 * Copyright (c) 2004 Colin Percival
 * Copyright (c) 2005 Nate Lawson
 * Copyright (c) 2010 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>

#include <stdio.h>
#include <stdlib.h>

#include "libdev.h"

static void
devd_notify_test(const struct devd_item *dev)
{
	unsigned int i;

	printf("Notify: %s %s %s\n", dev->notify.system, dev->notify.subsystem,
	    dev->notify.type);

	for (i = 0; i < dev->details_len; i++) {
		printf("\t%s=%s\n", dev->details[i].key, dev->details[i].value);
	}
}

static void
devd_test_devfs(const struct devd_item *dev)
{
	printf("Devfs notify: %s %s %s\n", dev->notify.system,
	    dev->notify.subsystem, dev->notify.type);
}

static void
devd_device_test(const struct devd_item *dev)
{
	const char *action;
	unsigned int i;

	switch(dev->type) {
	case DEV_ADD:
		action = "Add";
		break;

	case DEV_REMOVE:
		action = "Remove";
		break;

	case DEV_UNKNOWN:
		action = "Unknown device";
		break;

	default:
		/* Should never happen */
		return;
	}
	printf("%s %s on %s\n", action, dev->device.name, dev->device.parent);
	for (i = 0; i < dev->details_len; i++) {
		printf("\t%s=%s\n", dev->details[i].key, dev->details[i].value);
	}
}

static void
devd_test_umass(const struct devd_item *dev)
{
	printf("UMASS %s\n", dev->device.name);
}

int
main(int argc __unused, char *argv[] __unused)
{
	struct timeval timeout;
	fd_set fdset;
	int devd_fd, nfds;

	if ((devd_fd = devd_init()) < 0) {
		return 1;
	}

	devd_add_notify_callback("*", "*", "*", devd_notify_test);
	devd_add_notify_callback("DEVFS", "*", "*", devd_test_devfs);

	devd_add_device_callback("*", DEV_ADD | DEV_REMOVE, devd_device_test);
	devd_add_device_callback("umass*", DEV_ADD | DEV_REMOVE,
	    devd_test_umass);

	for(;;) {
		FD_ZERO(&fdset);
		FD_SET(devd_fd, &fdset);
		nfds = devd_fd + 1;

		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		select(nfds, &fdset, NULL, &fdset, &timeout);

		if (devd_read() != 0)
			break;
	}

	devd_close();
}

