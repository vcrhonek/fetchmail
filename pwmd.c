/*
    Copyright (C) Ben Kibbey <bjk@luxsci.net>

    For license terms, see the file COPYING in this directory.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <libpwmd.h>
#include "i18n.h"
#include "pwmd.h"

static void exit_with_pwmd_error(gpg_error_t rc)
{
    report(stderr, GT_("pwmd: ERR %i: %s\n"), rc, gpg_strerror(rc));

    if (pwm) {
	pwmd_close(pwm);
	pwm = NULL;
    }

    /* Don't exit if daemonized. There may be other active accounts. */
    if (isatty(STDOUT_FILENO))
	exit(PS_SYNTAX);
}

static gpg_error_t status_cb(void *data, const char *line)
{
    (void)data;
    if (!strncmp(line, "LOCKED", 6))
	report(stderr, "pwmd(%s): %s\n", pwmd_file, line);

    return 0;
}

#define MAX_PWMD_ARGS 8
static gpg_error_t parse_socket_args (const char *str, char ***args)
{
  gpg_error_t rc = 0;
  char buf[2048], *b;
  const char *p;
  int n = 0;
  static char *result[MAX_PWMD_ARGS] = {0};

  for (n = 0; n < MAX_PWMD_ARGS; n++)
      result[n] = NULL;

  n = 0;

  for (b = buf, p = str; *p; p++)
    {
      if (*p == ',')
	{
	  *b = 0;
	  result[n] = xstrdup (buf);
	  if (!result[n])
	    {
	      rc = gpg_error (GPG_ERR_ENOMEM);
	      break;
	    }

	  b = buf;
	  *b = 0;
	  n++;
	  continue;
	}

      *b++ = *p;
    }

  if (!rc && buf[0])
    {
      *b = 0;
      result[n] = xstrdup (buf);
      if (!result[n])
	  rc = gpg_error (GPG_ERR_ENOMEM);
    }

  if (rc)
    {
	for (n = 0; n < MAX_PWMD_ARGS; n++)
	{
	    xfree(result[n]);
	    result[n] = NULL;
	}
    }
  else
    *args = result;

  return rc;
}

int connect_to_pwmd(const char *socketname, const char *socket_args,
		    const char *filename)
{
    gpg_error_t rc;

    pwmd_init();

    /* Try to reuse an existing connection for another account on the same pwmd
     * server. If the socket name or any socket arguments have changed then
     * reopen the connection. */
    if (!pwm || (socketname && !pwmd_socket)
	    || (!socketname && pwmd_socket) ||
            (socketname && pwmd_socket && strcmp(socketname, pwmd_socket))
            || (socket_args && !pwmd_socket_args)
            || (!socket_args && pwmd_socket_args)
            || (socket_args && pwmd_socket_args
                && strcmp (socket_args, pwmd_socket_args))) {
	pwmd_close(pwm);
	rc = pwmd_new("fetchmail", &pwm);
	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}

	rc = pwmd_setopt (pwm, PWMD_OPTION_SOCKET_TIMEOUT, 120);
	if (!rc)
	    rc = pwmd_setopt(pwm, PWMD_OPTION_STATUS_CB, status_cb);

	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}

	if (socket_args) {
	    char **args = NULL;
	    int n;

	    rc = parse_socket_args (socket_args, &args);
	    if (!rc) {
		rc = pwmd_connect(pwm, socketname, args[0], args[1], args[2],
				  args[3], args[4], args[5], args[6], args[7]);

		for (n = 0; n < MAX_PWMD_ARGS; n++)
		    xfree(args[n]);
	    }
	}
	else
            rc = pwmd_connect(pwm, socketname, NULL);

	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}

	pwmd_setopt(pwm, PWMD_OPTION_PINENTRY_DESC, NULL);
        rc = pwmd_setopt (pwm, PWMD_OPTION_LOCK_TIMEOUT, 300);
	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}

	rc = pwmd_setopt(pwm, PWMD_OPTION_LOCK_ON_OPEN, 0);
	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}
    }

    if (run.pinentry_timeout > 0) {
	rc = pwmd_setopt(pwm, PWMD_OPTION_PINENTRY_TIMEOUT,
		run.pinentry_timeout);
	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}
    }

    if (!pwmd_file || strcmp(filename, pwmd_file)) {
	/* Temporarily set so status_cb knows what file. */
	pwmd_file = filename;
	rc = pwmd_open(pwm, filename, NULL, NULL);
	pwmd_file = NULL;
	if (rc) {
	    exit_with_pwmd_error(rc);
	    return 1;
	}
    }

    /* May be null to use the default of ~/.pwmd/socket. */
    pwmd_socket = socketname;
    pwmd_socket_args = socket_args;
    pwmd_file = filename;
    return 0;
}

static int
failure (const char *account, const char *id, gpg_error_t rc, int required)
{
    if (!rc)
        return 0;

    if (gpg_err_code (rc) == GPG_ERR_ELEMENT_NOT_FOUND) {
        report(stderr, GT_("%spwmd(%s): %s(%s): %s\n"),
               required ? "" : GT_("WARNING: "), pwmd_file, account, id,
               gpg_strerror(rc));
        if (!required)
            return 0;
    }

    return 1;
}

static gpg_error_t
alloc_result (const char *s, size_t len, char **result)
{
    if (!len) {
        *result = NULL;
        return 0;
    }

    char *b = xmalloc (len+1);

    if (!b)
        return GPG_ERR_ENOMEM;

    memcpy (b, s, len);
    b[len] = 0;
    *result = b;
    return 0;
}

int get_pwmd_elements(const char *account, int protocol, struct query *ctl)
{
    const char *prot1 = showproto(protocol);
    char *result, *prot;
    char *tmp = xstrdup(account);
    char *bulk = NULL;
    const char *bresult;
    size_t brlen, len;
    size_t offset = 0;
    char *str;
    gpg_error_t rcs[3] = { 0 };
    gpg_error_t rc, brc;
    int i;

    prot = xstrdup(prot1);

    for (i = 0; prot[i]; i++)
        prot[i] = tolower(prot[i]);

    for (i = 0; tmp[i]; i++) {
        if (i && tmp[i] == '^')
            tmp[i] = '\t';
    }

    rc = pwmd_bulk_append (&bulk, "NOP", 3, "NOP", NULL, 0, &offset);
    if (rc)
        goto done;

    rcs[0] = 0;
    rcs[1] = GPG_ERR_MISSING_ERRNO;
    str = pwmd_strdup_printf ("%s\t%s\thostname", tmp, prot);
    rc = pwmd_bulk_append_rc (&bulk, rcs, "HOST", 4, "GET", str, strlen (str),
                              &offset);
    pwmd_free (str);
    if (rc)
        goto done;

    /*
     * Server port. Fetchmail tries standard ports for known services so it
     * should be alright if this element isn't found. ctl->server.protocol is
     * already set. This sets ctl->server.service.
     */
    rcs[0] = 0;
    rcs[1] = GPG_ERR_MISSING_ERRNO;
    offset--;
    str = pwmd_strdup_printf ("%s\t%s\tport", tmp, prot);
    rc = pwmd_bulk_append_rc (&bulk, rcs, "PORT", 4, "GET", str, strlen (str),
                              &offset);
    pwmd_free (str);
    if (rc)
        goto done;

    rcs[0] = 0;
    rcs[1] = gpg_err_make (GPG_ERR_SOURCE_USER_1, GPG_ERR_ELEMENT_NOT_FOUND);
    rcs[2] = GPG_ERR_MISSING_ERRNO;
    offset--;
    str = pwmd_strdup_printf ("%s\tusername", tmp);
    rc = pwmd_bulk_append_rc (&bulk, rcs, "USER", 4, "GET", str, strlen (str),
                              &offset);
    pwmd_free (str);
    if (rc)
        goto done;

    rcs[0] = 0;
    rcs[1] = GPG_ERR_MISSING_ERRNO;
    offset--;
    str = pwmd_strdup_printf ("%s\tpassword", tmp);
    rc = pwmd_bulk_append_rc (&bulk, rcs, "PASS", 4, "GET", str, strlen (str),
                              &offset);
    pwmd_free (str);
    if (rc)
        goto done;

#ifdef SSL_ENABLE
    /* It is up to the user to specify the sslmode via the command line or via
     * the rcfile rather than requiring an extra element. */
    rcs[0] = 0;
    rcs[1] = gpg_err_make (GPG_ERR_SOURCE_USER_1, GPG_ERR_ELEMENT_NOT_FOUND);
    rcs[2] = GPG_ERR_MISSING_ERRNO;
    offset--;
    str = pwmd_strdup_printf ("%s\t%s\tsslfingerprint", tmp, prot);
    rc = pwmd_bulk_append_rc (&bulk, rcs, "SSL", 3, "GET", str, strlen (str),
                              &offset);
    pwmd_free (str);
    if (rc)
        goto done;
#endif

    rc = pwmd_bulk_finalize (&bulk);
    if (!rc)
        rc = pwmd_bulk (pwm, &result, &len, NULL, NULL, bulk, strlen (bulk));
    if (rc)
        goto done;

    offset = 0;
    rc = pwmd_bulk_result (result, len, "HOST", 4, &offset, &bresult, &brlen,
                           &brc);
    if (failure (account, "hostname", rc ? rc : brc, 1))
        goto done;

    if (ctl->server.pollname != ctl->server.via)
      xfree(ctl->server.via);

    alloc_result (bresult, brlen, &ctl->server.via);
    xfree(ctl->server.queryname);
    ctl->server.queryname = xstrdup(ctl->server.via);
    xfree(ctl->server.truename);
    ctl->server.truename = xstrdup(ctl->server.queryname);

    rc = pwmd_bulk_result (result, len, "PORT", 4, &offset, &bresult, &brlen,
                           &brc);
    if (failure (account, "port", rc ? rc : brc, 0))
        goto done;
    xfree(ctl->server.service);
    ctl->server.service = NULL;
    if (brlen)
        alloc_result (bresult, brlen, &ctl->server.service);

    rc = pwmd_bulk_result (result, len, "USER", 4, &offset, &bresult, &brlen,
                           &brc);
    if (failure (account, "username", rc ? rc : brc, 1))
        goto done;

    xfree(ctl->remotename);
    xfree(ctl->server.esmtp_name);
    ctl->remotename = ctl->server.esmtp_name = NULL;
    alloc_result (bresult, brlen, &ctl->remotename);
    alloc_result (bresult, brlen, &ctl->server.esmtp_name);

    rc = pwmd_bulk_result (result, len, "PASS", 4, &offset, &bresult, &brlen,
                           &brc);
    if (failure (account, "password", rc ? rc : brc, !isatty (STDOUT_FILENO)))
        goto done;

    xfree(ctl->password);
    ctl->password = NULL;
    if (brlen)
        alloc_result (bresult, brlen, &ctl->password);
    brc = 0;

#ifdef SSL_ENABLE
    rc = pwmd_bulk_result (result, len, "SSL", 3, &offset, &bresult, &brlen,
                           &brc);
    if (failure (account, "sslfingerprint", rc ? rc : brc, 0))
        goto done;
    xfree(ctl->sslfingerprint);
    ctl->sslfingerprint = NULL;
    if (brlen)
        alloc_result (bresult, brlen, &ctl->sslfingerprint);
    brc = 0;
#endif

done:
    pwmd_free (result);
    pwmd_free (bulk);
    xfree(tmp);
    xfree(prot);

    if (rc || brc)
        exit_with_pwmd_error (rc ? rc : brc);

    return !!rc || !!brc;
}
