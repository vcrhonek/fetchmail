/*
 * pop3.c -- POP3 protocol methods
 *
 * For license terms, see the file COPYING in this directory.
 */

#include  "config.h"

#include  <stdio.h>
#include  <string.h>
#include  <ctype.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(STDC_HEADERS)
#include  <stdlib.h>
#endif
 
#include  "fetchmail.h"
#include  "socket.h"

#if HAVE_LIBOPIE
#include  <opie.h>
#endif /* HAVE_LIBOPIE */

#define PROTOCOL_ERROR	{error(0, 0, "protocol error"); return(PS_ERROR);}

extern char *strstr();	/* needed on sysV68 R3V7.1. */

static int last;

#if HAVE_LIBOPIE
static char lastok[POPBUFSIZE+1];
#endif /* HAVE_LIBOPIE */

int pop3_ok (int sock, char *argbuf)
/* parse command response */
{
    int ok;
    char buf [POPBUFSIZE+1];
    char *bufp;

    if ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
    {
	bufp = buf;
	if (*bufp == '+' || *bufp == '-')
	    bufp++;
	else
	    return(PS_PROTOCOL);

	while (isalpha(*bufp))
	    bufp++;

	if (*bufp)
	  *(bufp++) = '\0';

	if (strcmp(buf,"+OK") == 0)
	{
#if HAVE_LIBOPIE
	    strcpy(lastok, bufp);
#endif /* HAVE_LIBOPIE */
	    ok = 0;
	}
	else if (strcmp(buf,"-ERR") == 0)
	{
	    /*
	     * We're checking for "lock busy", "unable to lock", 
	     * "already locked" etc. here.  This indicates that we
	     * have to wait for the server to clean up before we
	     * can poll again.
	     *
	     * PS_LOCKBUSY check empirically verified with two recent
	     * versions the Berkeley popper;	QPOP (version 2.2)  and
	     * QUALCOMM Pop server derived from UCB (version 2.1.4-R3)
	     */
	    if (strstr(bufp,"lock")||strstr(bufp,"Lock")||strstr(bufp,"LOCK"))
		ok = PS_LOCKBUSY;
	    else
		ok = PS_AUTHFAIL;
	    if (*bufp)
	      error(0,0,bufp);
	}
	else
	    ok = PS_PROTOCOL;

	if (argbuf != NULL)
	    strcpy(argbuf,bufp);
    }

    return(ok);
}

int pop3_getauth(int sock, struct query *ctl, char *greeting)
/* apply for connection authorization */
{
    int ok;
    char *start,*end;
    char *msg;
#if HAVE_LIBOPIE
    char *challenge;
#endif /* HAVE_LIBOPIE */

    switch (ctl->server.protocol) {
    case P_POP3:
	if ((gen_transact(sock, "USER %s", ctl->remotename)) != 0)
	    PROTOCOL_ERROR

#if defined(HAVE_LIBOPIE) && defined(OPIE_ENABLE)
	/* see RFC1938: A One-Time Password System */
	if (challenge = strstr(lastok, "otp-"))
	{
	    char response[OPIE_RESPONSE_MAX+1];

	    if (opiegenerator(challenge, ctl->password, response))
		 PROTOCOL_ERROR

	    ok = gen_transact(sock, "PASS %s", response);
	}
	else
#endif /* defined(HAVE_LIBOPIE) && defined(OPIE_ENABLE) */
	    /* ordinary validation, no one-time password */ 
	    ok = gen_transact(sock, "PASS %s", ctl->password);
	break;

    case P_APOP:
	/* build MD5 digest from greeting timestamp + password */
	/* find start of timestamp */
	for (start = greeting;  *start != 0 && *start != '<';  start++)
	    continue;
	if (*start == 0) {
	    error(0, -1, "Required APOP timestamp not found in greeting");
	    return(PS_AUTHFAIL);
	}

	/* find end of timestamp */
	for (end = start;  *end != 0  && *end != '>';  end++)
	    continue;
	if (*end == 0 || end == start + 1) {
	    error(0, -1, "Timestamp syntax error in greeting");
	    return(PS_AUTHFAIL);
	}
	else
	    *++end = '\0';

	/* copy timestamp and password into digestion buffer */
	msg = (char *)xmalloc((end-start+1) + strlen(ctl->password) + 1);
	strcpy(msg,start);
	strcat(msg,ctl->password);

	strcpy(ctl->digest, MD5Digest(msg));
	free(msg);

	ok = gen_transact(sock, "APOP %s %s", ctl->remotename, ctl->digest);
	break;

    case P_RPOP:
	if ((gen_transact(sock,"USER %s", ctl->remotename)) != 0)
	    PROTOCOL_ERROR

	ok = gen_transact(sock, "RPOP %s", ctl->password);
	break;

    default:
	error(0, 0, "Undefined protocol request in POP3_auth");
	ok = PS_ERROR;
    }

    /* maybe we detected a lock-busy condition? */
    if (ok != 0)
    {
        if (ok == PS_LOCKBUSY)
	{
	    error(0, 0, "lock busy!  Is another session active?"); 
	    return(PS_LOCKBUSY);
	}
	PROTOCOL_ERROR
    }

    /*
     * Empirical experience shows some server/OS combinations
     * may need a brief pause even after any lockfiles on the
     * server are released, to give the server time to finish
     * copying back very large mailfolders from the temp-file...
     * this is only ever an issue with extremely large mailboxes.
     */
    sleep(3); /* to be _really_ safe, probably need sleep(5)! */

    /* we're approved */
    return(0);
}

static int
pop3_gettopid( int sock, int num , char *id)
{
    int ok;
    int got_it;
    char buf [POPBUFSIZE+1];
    sprintf( buf, "TOP %d 1", num );
    if( (ok = gen_transact(sock, buf ) ) != 0 )
       return ok; 
    got_it = 0;
    while((ok = gen_recv(sock, buf, sizeof(buf))) == 0) {
	if( buf[0] == '.' )
	    break;
	if( ! got_it && ! strncasecmp("Message-Id:", buf, 11 )) {
	    got_it = 1;
	    sscanf( buf+12, "%s", id);
	}
    }
    return 0;
}

static int
pop3_slowuidl( int sock,  struct query *ctl, int *countp, int *newp)
{
    /* This approach tries to get the message headers from the
     * remote hosts and compares the message-id to the already known
     * ones:
     *  + if the first message containes a new id, all messages on
     *    the server will be new
     *  + if the first is known, try to estimate the last known message
     *    on the server and check. If this works you know the total number
     *    of messages to get.
     *  + Otherwise run a binary search to determine the last known message
     */
    int ok, nolinear = 0;
    int first_nr, list_len, try_id, try_nr, hop_id, add_id;
    int num;
    char id [IDLEN+1];
    
    if( (ok = pop3_gettopid( sock, 1, id )) != 0 )
	return ok;
    
    if( ( first_nr = str_nr_in_list(&ctl->oldsaved, id) ) == -1 ) {
	/* the first message is unknown -> all messages are new */
	*newp = *countp;	
	return 0;
    }

    /* check where we expect the latest known message */
    list_len = count_list( &ctl->oldsaved );
    try_id = list_len  - first_nr; /* -1 + 1 */
    if( try_id > 1 ) {
	if( try_id <= *countp ) {
	    if( (ok = pop3_gettopid( sock, try_id, id )) != 0 )
		return ok;
    
	    try_nr = str_nr_in_list(&ctl->oldsaved, id);
	} else {
	    try_id = *countp+1;
	    try_nr = -1;
	}
	if( try_nr != list_len -1 ) {
	    /* some messages inbetween have been deleted... */
	    if( try_nr == -1 ) {
		nolinear = 1;

		for( add_id = 1<<30; add_id > try_id-1; add_id >>= 1 )
		    ;
		for( ; add_id; add_id >>= 1 ) {
		    if( try_nr == -1 ) {
			if( try_id - add_id <= 1 ) {
			    continue;
			}
			try_id -= add_id;
		    } else 
			try_id += add_id;
		    
		    if( (ok = pop3_gettopid( sock, try_id, id )) != 0 )
			return ok;
		    try_nr = str_nr_in_list(&ctl->oldsaved, id);
		}
		if( try_nr == -1 ) {
		    try_id--;
		}
	    } else {
		error(0,0,"Messages inserted into list on server. "
		      "Cannot handle this.");
		return -1;
	    }
	} 
    }
    /* The first try_id messages are known -> copy them to
       the newsaved list */
    for( num = first_nr; num < list_len; num++ )
	save_str(&ctl->newsaved, num-first_nr + 1,
		 str_from_nr_list( &ctl->oldsaved, num ));

    if( nolinear ) {
	free_str_list(&ctl->oldsaved);
	ctl->oldsaved = 0;
	last = try_id;
    }

    *newp = *countp - try_id;
    return 0;
}

static int pop3_getrange(int sock, 
			 struct query *ctl,
			 const char *folder,
			 int *countp, int *newp)
/* get range of messages to be fetched */
{
    int ok;
    char buf [POPBUFSIZE+1];

    /* Ensure that the new list is properly empty */
    ctl->newsaved = (struct idlist *)NULL;

#ifdef MBOX
    /* Alain Knaff suggests this, but it's not RFC standard */
    if (folder)
	if ((ok = gen_transact(sock, "MBOX %s", folder)))
	    return ok;
#endif /* MBOX */

    /* get the total message count */
    gen_send(sock, "STAT");
    ok = pop3_ok(sock, buf);
    if (ok == 0)
	sscanf(buf,"%d %*d", countp);
    else
	return(ok);

    /*
     * Newer, RFC-1725-conformant POP servers may not have the LAST command.
     * We work as hard as possible to hide this ugliness, but it makes 
     * counting new messages intrinsically quadratic in the worst case.
     */
    last = 0;
    *newp = -1;
    if (*countp > 0 && !ctl->fetchall)
    {
	char id [IDLEN+1];

	if (!ctl->server.uidl) {
	    gen_send(sock, "LAST");
	    ok = pop3_ok(sock, buf);
	} else
	    ok = 1;
	if (ok == 0)
	{
	    if (sscanf(buf, "%d", &last) == 0)
		PROTOCOL_ERROR
	    *newp = (*countp - last);
	}
 	else
 	{
	    /* grab the mailbox's UID list */
	    if ((ok = gen_transact(sock, "UIDL")) != 0)
	    {
		/* don't worry, yet! do it the slow way */
		if((ok = pop3_slowuidl( sock, ctl, countp, newp))!=0)
		    PROTOCOL_ERROR
	    }
	    else
	    {
		int	num;

		*newp = 0;
 		while ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
		{
 		    if (buf[0] == '.')
 			break;
 		    else if (sscanf(buf, "%d %s", &num, id) == 2)
		    {
 			save_str(&ctl->newsaved, num, id);

			/* note: ID comparison is caseblind */
			if (!str_in_list(&ctl->oldsaved, id))
			    (*newp)++;
		    }
 		}
 	    }
 	}
    }

    return(0);
}

static int pop3_getsizes(int sock, int count, int *sizes)
/* capture the sizes of all messages */
{
    int	ok;

    if ((ok = gen_transact(sock, "LIST")) != 0)
	return(ok);
    else
    {
	char buf [POPBUFSIZE+1];

	while ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
	{
	    int num, size;

	    if (buf[0] == '.')
		break;
	    else if (sscanf(buf, "%d %d", &num, &size) == 2)
		sizes[num - 1] = size;
	    else
		sizes[num - 1] = -1;
	}

	return(ok);
    }
}

static int pop3_is_old(int sock, struct query *ctl, int num)
/* is the given message old? */
{
    if (!ctl->oldsaved)
	return (num <= last);
    else
	/* note: ID comparison is caseblind */
        return (str_in_list(&ctl->oldsaved,
			    str_find (&ctl->newsaved, num)));
}

static int pop3_fetch(int sock, struct query *ctl, int number, int *lenp)
/* request nth message */
{
    int ok;
    char buf [POPBUFSIZE+1], *cp;

    gen_send(sock, "RETR %d", number);
    if ((ok = pop3_ok(sock, buf)) != 0)
	return(ok);

    *lenp = -1;		/* we got sizes from the LIST response */

    return(0);
}

static int pop3_delete(int sock, struct query *ctl, int number)
/* delete a given message */
{
    /* actually, mark for deletion -- doesn't happen until QUIT time */
    return(gen_transact(sock, "DELE %d", number));
}

static int pop3_logout(int sock, struct query *ctl)
/* send logout command */
{
    return(gen_transact(sock, "QUIT"));
}

const static struct method pop3 =
{
    "POP3",		/* Post Office Protocol v3 */
    110,		/* standard POP3 port */
    FALSE,		/* this is not a tagged protocol */
    TRUE,		/* this uses a message delimiter */
    pop3_ok,		/* parse command response */
    pop3_getauth,	/* get authorization */
    pop3_getrange,	/* query range of messages */
    pop3_getsizes,	/* we can get a list of sizes */
    pop3_is_old,	/* how do we tell a message is old? */
    pop3_fetch,		/* request given message */
    NULL,		/* no way to fetch body alone */
    NULL,		/* no message trailer */
    pop3_delete,	/* how to delete a message */
    pop3_logout,	/* log out, we're done */
    FALSE,		/* no, we can't re-poll */
};

int doPOP3 (struct query *ctl)
/* retrieve messages using POP3 */
{
#ifndef MBOX
    if (ctl->mailboxes->id) {
	fprintf(stderr,"Option --remote is not supported with POP3\n");
	return(PS_SYNTAX);
    }
#endif /* MBOX */
    peek_capable = FALSE;
    return(do_protocol(ctl, &pop3));
}

/* pop3.c ends here */
