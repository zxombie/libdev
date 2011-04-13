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

#include <sys/types.h>

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "libdev.h"

#define DEVCTL_MAXBUF	1024
#define DEVDPIPE	"/var/run/devd.pipe"

struct devd_callback {
	SLIST_ENTRY(devd_callback) entries;
	int		 types;

	/* Use for notifications */
	char		*system;
	char		*subsystem;
	char		*type;

	/* Used for devices */
	char		*dev_name;

	devd_callback	*callback;
};

SLIST_HEAD(, devd_callback) cb_head;

static int	devd_off = 0;
static char	devd_buf[DEVCTL_MAXBUF];
static int	devd_pipe = -1;

int
devd_init(void)
{
	struct sockaddr_un devd_addr;

	bzero(&devd_addr, sizeof(devd_addr));
	if ((devd_pipe = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
		return (-1);
	}

	devd_addr.sun_family = PF_LOCAL;
	strlcpy(devd_addr.sun_path, DEVDPIPE, sizeof(devd_addr.sun_path));
	if (connect(devd_pipe, (struct sockaddr *)&devd_addr,
	    sizeof(devd_addr)) == -1) {
		close(devd_pipe);
		devd_pipe = -1;
		return (-1);
	}

	if (fcntl(devd_pipe, F_SETFL, O_NONBLOCK) == -1) {
		close(devd_pipe);
		return (-1);
	}

	return (devd_pipe);
}

void
devd_close(void)
{

	close(devd_pipe);
	devd_pipe = -1;
}

static struct devd_callback *
devd_callback_create(unsigned int types, devd_callback *callback)
{
	struct devd_callback *devd_cb;

	/* Create the struct to hold the callback details */
	devd_cb = calloc(sizeof(*devd_cb), 1);
	if (devd_cb == NULL)
		return NULL;

	/* And populate it */
	devd_cb->callback = callback;
	devd_cb->types = types;

	return devd_cb;
}

int
devd_add_notify_callback(const char *systm, const char *subsystem,
    const char *type, devd_callback *callback)
{
	struct devd_callback *devd_cb;

	/* Create the struct to hold the callback details */
	devd_cb = devd_callback_create(DEV_NOTIFY, callback);
	if (devd_cb == NULL)
		return 1;

	devd_cb->system = strdup(systm);
	if (devd_cb->system == NULL)
		goto err;

	devd_cb->subsystem = strdup(subsystem);
	if (devd_cb->subsystem == NULL)
		goto err;

	devd_cb->type = strdup(type);
	if (devd_cb->type == NULL)
		goto err;

	/* Add the item to the queue */
	SLIST_INSERT_HEAD(&cb_head, devd_cb, entries);

	return 0;

err:
	free(devd_cb->system);
	free(devd_cb->subsystem);
	free(devd_cb->type);
	free(devd_cb);

	return 1;
}

int
devd_add_device_callback(const char *dev_name, int types,
    devd_callback *callback)
{
	struct devd_callback *devd_cb;

	/* Check invalid types */
	if ((types & ~(DEV_DEVICE_ALL)) != 0)
		return 1;

	/* Create the struct to hold the callback details */
	devd_cb = devd_callback_create(types, callback);
	if (devd_cb == NULL)
		return 1;

	devd_cb->dev_name = strdup(dev_name);
	if (devd_cb->dev_name == NULL) {
		free(devd_cb);
		return 1;
	}

	/* Add the item to the queue */
	SLIST_INSERT_HEAD(&cb_head, devd_cb, entries);

	return 0;
}

static int
devd_match(const char *pattern, const char *str)
{
	size_t p_pos, s_pos;

	s_pos = p_pos = 0;

	while(pattern[p_pos] != '\0' && str[s_pos] != '\0') {
		if (pattern[p_pos] == '*') {
			if (pattern[p_pos + 1] == str[s_pos]) {
				/* If the character after the '*'
				 * matches the current char in the
				 * test string stop processing the '*'.
				 */
				p_pos += 2;
			} else if (str[s_pos + 1] == '\0') {
				/*
				 * If the we are on the last char move p_pos
				 * so it the check it is null will work.
				 */
				p_pos++;
			}
		} else {
			/* The pattern doesn't match, exit. */
			if (pattern[p_pos] != str[s_pos])
				return 1;

			p_pos++;
		}
		s_pos++;
	}

	if (pattern[p_pos] != '\0' || str[s_pos] != '\0')
		return 1;

	return 0;
}

static int
devd_set_details(char *buf, struct devd_details **out_details, size_t *out_len)
{
	struct devd_details *details;
	size_t details_len, len;
	char *ptr, *sep;

	details = NULL;
	len = 0;
	details_len = 0;

	while(buf != NULL) {
		/* Find the space separating the details */
		ptr = strchr(buf, ' ');

		/* Null terminate the details string */
		if (ptr != NULL) {
			ptr[0] = '\0';
			ptr++;
		}

		/* Are the details empty */
		if (buf[0] == '\0')
			break;

		sep = strchr(buf, '=');
		if (sep != NULL) {
			sep[0] = '\0';
			sep++;

			if (len == details_len) {
				struct devd_details *tmp;

				details_len += 2;
				tmp = realloc(details,
				    details_len * sizeof(details[0]));
				if (tmp == NULL)
					return 1;
				details = tmp;
			}
			details[len].key = buf;
			details[len].value = sep;
			len += 1;
		} else {
			return 1;
		}

		buf = ptr;
	}

	*out_details = details;
	*out_len = len;

	return 0;
}

static void
devd_notify_process(char *buf)
{
	struct devd_callback *cb;
	struct devd_item dev;
	char *sys_ptr, *subsys_ptr, *type_ptr;
	char *ptr;

	if (buf[0] != '!')
		return;

	/* Find the required parts of the string */
	sys_ptr = strstr(buf, "system=");
	if (sys_ptr == NULL)
		return;

	subsys_ptr = strstr(buf, "subsystem=");
	if (subsys_ptr == NULL)
		return;

	type_ptr = strstr(buf, "type=");
	if (type_ptr == NULL)
		return;

	/* Make each part null terminated */
	ptr = strchr(sys_ptr, ' ');
	if (ptr != NULL)
		ptr[0] = '\0';
	sys_ptr += strlen("system=");

	ptr = strchr(subsys_ptr, ' ');
	if (ptr != NULL)
		ptr[0] = '\0';
	subsys_ptr += strlen("subsystem=");

	ptr = strchr(type_ptr, ' ');
	if (ptr != NULL)
		ptr[0] = '\0';
	type_ptr += strlen("type=");

	dev.type = DEV_NOTIFY;
	dev.notify.system = sys_ptr;
	dev.notify.subsystem = subsys_ptr;
	dev.notify.type = type_ptr;

	if (ptr != NULL) {
		int ret;

		buf = ptr + 1;
		ret = devd_set_details(buf, &dev.details, &dev.details_len);
		if (ret != 0)
			goto done;
	} else {
		dev.details = NULL;
		dev.details_len = 0;
	}

	SLIST_FOREACH(cb, &cb_head, entries) {
		/* Skip callbacks that dont expect this type */
		if ((cb->types & DEV_NOTIFY) != DEV_NOTIFY)
			continue;

		if (devd_match(cb->system, dev.notify.system) != 0)
			continue;

		if (devd_match(cb->subsystem, dev.notify.subsystem) != 0)
			continue;

		if (devd_match(cb->type, dev.notify.type) != 0)
			continue;

		cb->callback(&dev);
	}

done:
	free(dev.details);
}

static void
devd_device_process(char *buf)
{
	struct devd_callback *cb;
	struct devd_item dev;
	char *at_ptr, *on_ptr;
	char *ptr;
	int ret;

	switch(buf[0]) {
	case '+':
		dev.type = DEV_ADD;
		break;

	case '-':
		dev.type = DEV_REMOVE;
		break;

	case '?':
		dev.type = DEV_UNKNOWN;
		break;

	default:
		return;
	};

	buf++;

	/* Split the string into its parts */
	at_ptr = strstr(buf, " at ");
	if (at_ptr == NULL)
		return;
	at_ptr[0] = '\0';
	at_ptr += 4;

	on_ptr = strstr(at_ptr, " on ");
	if (on_ptr == NULL)
		return;
	on_ptr[0] = '\0';
	on_ptr += 4;

	/* Null terminate the name */
	ptr = strchr(buf, ' ');
	if (ptr != NULL)
		ptr[0] = '\0';

	dev.device.name = buf;

	/* Everything after the ' on ' is the parent */
	dev.device.parent = on_ptr;

	/* Remove empty space before the details */
	buf = at_ptr;
	while(buf[0] == ' ')
		buf++;

	ret = devd_set_details(buf, &dev.details, &dev.details_len);
	if (ret != 0)
		goto done;

	SLIST_FOREACH(cb, &cb_head, entries) {
		/* Skip callbacks that dont expect this type */
		if ((dev.type & cb->types & DEV_DEVICE_ALL) == 0)
			continue;

		if (cb->dev_name == NULL)
			continue;

		if (devd_match(cb->dev_name, dev.device.name) != 0)
			continue;


		cb->callback(&dev);
	}

done:
	free(dev.details);
}

int
devd_read(void)
{
	char *ptr;
	ssize_t rlen;

	rlen = read(devd_pipe, &devd_buf[devd_off], sizeof(devd_buf));
	if (rlen == 0 || (rlen < 0 && errno != EWOULDBLOCK)) {
		devd_close();
		return 1;
	}
	if (rlen > 0) {
		devd_off += rlen;

		/* Find the terminating newline */
		ptr = memchr(devd_buf, '\n', devd_off);
		if (ptr == NULL) {
			/* No newline found yet */
			return 0;
		}
		ptr[0] = '\0';
		ptr++;

		switch(devd_buf[0]) {
		case '+':
		case '-':
		case '?':
			devd_device_process(devd_buf);
			break;

		case '!':
			devd_notify_process(devd_buf);
			break;
		}

		if (ptr - devd_buf < rlen) {
			size_t move_len;

			move_len = rlen - (ptr - devd_buf);
			memmove(devd_buf, ptr, move_len);
		} else {
			assert(ptr - devd_buf == rlen);
		}
		devd_off = 0;
	}

	return 0;
}

