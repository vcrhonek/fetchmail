/*
 * oauth2.c -- oauthbearer and xoauth2 support
 *
 * Copyright 2017 by Matthew Ogilvie
 * For license terms, see the file COPYING in this directory.
 */

#include "config.h"
#include "fetchmail.h"
#include "oauth2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *get_oauth2_string(struct query *ctl,flag xoauth2)
{
    /* Implements the bearer token string based for a
     * combination of RFC-7628 (ouath sasl, with
     * examples for imap only), RFC-6750 (oauth2), and
     * RFC-5034 (pop sasl), as implemented by gmail and others.
     *
     * Also supports xoauth2, which is just a couple of minor variariations.
     * https://developers.google.com/gmail/imap/xoauth2-protocol
     *
     * This assumes something external manages obtaining an up-to-date
     * authentication/bearer token and arranging for it to be in
     * ctl->password.  This may involve renewing it ahead of time if
     * necessary using a renewal token that fetchmail knows nothing about.
     * See:
     * https://github.com/google/gmail-oauth2-tools/wiki/OAuth2DotPyRunThrough
     */
    char *oauth2str;
    int oauth2len;

    char *oauth2b64;
    size_t oauth2b64alloc;

    oauth2len = strlen(ctl->remotename) + strlen(ctl->password) + 32;
    oauth2str = (char *)xmalloc(oauth2len);
    if (xoauth2)
    {
	snprintf(oauth2str, oauth2len,
		 "user=%s\1auth=Bearer %s\1\1",
		 ctl->remotename,
		 ctl->password);
    }
    else
    {
	snprintf(oauth2str, oauth2len,
		 "n,a=%s,\1auth=Bearer %s\1\1",
		 ctl->remotename,
		 ctl->password);
    }

    oauth2b64alloc = query_to64_outsize(strlen(oauth2str));
    oauth2b64 = (char *)xmalloc(oauth2b64alloc);
    to64frombits(oauth2b64, oauth2str, strlen(oauth2str), oauth2b64alloc);

    memset(oauth2str, 0x55, strlen(oauth2str));
    free(oauth2str);

    return oauth2b64;
}
