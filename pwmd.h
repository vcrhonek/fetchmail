/*
    Copyright (C) Ben Kibbey <bjk@luxsci.net>

    For license terms, see the file COPYING in this directory.
*/
#ifndef PWMD_H
#define PWMD_H

#include "fetchmail.h"

int connect_to_pwmd(const char *socketname, const char *socket_args,
		    const char *filename);
int get_pwmd_elements(const char *account, int protocol, struct query *ctl);
void disconnect_pwmd ();

#endif
