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
	char		*pattern;
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

int
devd_add_callback(const char *pattern, int types, devd_callback *callback)
{
	struct devd_callback *devd_cb;

	/* Check invalid types */
	if ((types & ~(DEV_ALL)) != 0)
		return 1;

	/* Create the struct to hold the callback details */
	devd_cb = malloc(sizeof(*devd_cb));
	if (devd_cb == NULL)
		return 1;

	/* And populate it */
	devd_cb->callback = callback;
	devd_cb->types = types;

	devd_cb->pattern = strdup(pattern);
	if (devd_cb->pattern == NULL) {
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

static void
devd_process(char *buf)
{
	struct devd_callback *cb;
	struct devd_item dev;
	size_t details_len;
	char *at_ptr, *on_ptr;
	char *ptr, *sep;

	switch(buf[0]) {
	case '+':
		dev.type = DEV_ADD;
		break;

	case '-':
		dev.type = DEV_REMOVE;
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

	dev.name = buf;

	/* Everything after the ' on ' is the parent */
	dev.parent = on_ptr;

	/* Remove empty space before the details */
	buf = at_ptr;
	while(buf[0] == ' ')
		buf++;

	/* Find the details on this device */
	dev.details = NULL;
	dev.details_len = 0;
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

			if (dev.details_len == details_len) {
				struct devd_details *tmp;

				details_len += 2;
				tmp = realloc(dev.details,
				    details_len * sizeof(dev.details[0]));
				if (tmp == NULL) {
					goto done;
				}
				dev.details = tmp;
			}
			dev.details[dev.details_len].key = buf;
			dev.details[dev.details_len].value = sep;
			dev.details_len++;
		} else {
			goto done;
		}

		buf = ptr;
	}

	SLIST_FOREACH(cb, &cb_head, entries) {
		/* Skip callbacks that dont expect this type */
		if ((dev.type & cb->types) == 0)
			continue;

		if (devd_match(cb->pattern, dev.name) != 0)
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
			devd_process(devd_buf);
			break;

		case '-':
			devd_process(devd_buf);
			break;

		case '!':
			break;

		case '?':
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

