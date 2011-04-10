/*
 * Copyright (C) 2011 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _LIBDEV_LIBDEV_H_
#define _LIBDEV_LIBDEV_H_

int	devd_init(void);
void	devd_close(void);
int	devd_read(void);

#define DEV_NOTIFY	0x01
#define	DEV_ADD		0x02
#define	DEV_REMOVE	0x04
#define	DEV_UNKNOWN	0x08
#define	DEV_ALL		0x0F

struct devd_details {
	const char *key;
	const char *value;
};

struct devd_item {
	int type;
	const char *name;

	struct devd_details *details;
	size_t details_len;

	const char *parent;
};

typedef	void devd_callback(const struct devd_item *);

int	devd_add_callback(const char *pattern, int types,
	    devd_callback *callback);

#endif /* _LIBDEV_LIBDEV_H_ */

