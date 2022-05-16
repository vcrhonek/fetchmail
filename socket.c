/*
 * socket.c -- socket library functions
 *
 * Copyright 1998 - 2004 by Eric S. Raymond.
 * Copyright 2004 - 2020 by Matthias Andree.
 * Contributions by Alexander Bluhm, Earl Chew, John Beck.

 * For license terms, see the file COPYING in this directory.
 */

#include "config.h"
#include "fetchmail.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h> /* isspace() */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>

#include "socket.h"
#include "i18n.h"
#include "sdump.h"
#include "uid_db.h"

/* Defines, these used to be used to allow BeOS and Cygwin to play nicely...
   these days, fetchmail requires a POSIX conforming system. */
#define fm_close(a)	close(a)
#define fm_write(a,b,c)	write(a,b,c)
#define fm_peek(a,b,c)	recv(a,b,c, MSG_PEEK)
#define fm_read(a,b,c)	read(a,b,c)

/* We need to define h_errno only if it is not already */
#ifndef h_errno
# if !HAVE_DECL_H_ERRNO
extern int h_errno;
# endif
#endif /* ndef h_errno */

/* used by SSL_get_ex_new_index, SSL_set_ex_data, SSL_get_ex_data, to 
 * communicate options and state with the verify callback */
static int global_mydata_index = -2;

static void free_plugindata(char **argvec)
{
    if (argvec) {
	xfree(*argvec);
	xfree(argvec);
    }
}

/** parse plugin and interpolate %h and %p with single-quoted host and service.  
 * Returns a malloc()ed pointer to a NULL-terminated vector of pointers, of 
 * which the first is also malloc()ed and the 2nd and later ones (if present) 
 * are pointers into the same memory region - these serve as input for the 
 * argument vector of execvp() in handle_plugin. */
static char **parse_plugin(const char *plugin, const char *host, const char *service)
{
	char **argvec;
	const char *c, *p;
	char *cp, *plugin_copy;
	unsigned int plugin_copy_len;
	unsigned int plugin_offset = 0, plugin_copy_offset = 0;
	unsigned int i, vecsiz = 2 * sizeof(char*), host_count = 0, service_count = 0;
	unsigned int plugin_len = strlen(plugin);
	unsigned int host_len = strlen(host);
	unsigned int service_len = strlen(service);

	for (c = p = plugin; *c; c++)
	{	if (isspace((unsigned char)*c) && !isspace((unsigned char)*p))
			vecsiz += sizeof(char*);
		if (*p == '%' && *c == 'h')
			host_count++;
		if (*p == '%' && *c == 'p')
			service_count++;
		p = c;
	}

	/* we need to discount 2 bytes for each placeholder */
	plugin_copy_len = plugin_len + (host_len - 2) * host_count + (service_len - 2) * service_count;
	plugin_copy = (char *)xmalloc(plugin_copy_len + 1);

	while (plugin_offset < plugin_len && plugin_copy_offset < plugin_copy_len)
	{	if ((plugin[plugin_offset] == '%') && (plugin[plugin_offset + 1] == 'h'))
		{	strcpy(plugin_copy + plugin_copy_offset, host);
			plugin_offset += 2;
			plugin_copy_offset += host_len;
		}
		else if ((plugin[plugin_offset] == '%') && (plugin[plugin_offset + 1] == 'p'))
		{	strcpy(plugin_copy + plugin_copy_offset, service);
			plugin_offset += 2;
			plugin_copy_offset += service_len;
		}
		else
		{	plugin_copy[plugin_copy_offset] = plugin[plugin_offset];
			plugin_offset++;
			plugin_copy_offset++;
		}
	}
	plugin_copy[plugin_copy_offset] = 0;

	/* XXX FIXME - is this perhaps a bit too simplistic to chop down the argument strings without any respect to quoting?
	 * better write a generic function that tracks arguments instead... */
	argvec = (char **)malloc(vecsiz);
	if (!argvec)
	{
		free(plugin_copy);
		report(stderr, GT_("fetchmail: malloc failed\n"));
		return NULL;
	}
	memset(argvec, 0, vecsiz);
	argvec[0] = plugin_copy; /* make sure we can free() it in every case */
	for (p = cp = plugin_copy, i = 0; *cp; cp++)
	{	if ((!isspace((unsigned char)*cp)) && (cp == p ? 1 : isspace((unsigned char)*p))) {
			argvec[i] = cp;
			i++;
		}
		p = cp;
	}
	for (cp = plugin_copy; *cp; cp++)
	{	if (isspace((unsigned char)*cp))
			*cp = 0;
	}
	return argvec;
}

static int handle_plugin(const char *host,
			 const char *service, const char *plugin)
/* get a socket mediated through a given external command */
{
    int fds[2];
    char **argvec;

    /*
     * The author of this code, Felix von Leitner <felix@convergence.de>, says:
     * he chose socketpair() instead of pipe() because socketpair creates 
     * bidirectional sockets while allegedly some pipe() implementations don't.
     */
    argvec = parse_plugin(plugin,host,service);
    if (!argvec || !*argvec[0]) {
	free_plugindata(argvec);
	report(stderr, GT_("fetchmail: plugin for host %s service %s is empty, cannot run!\n"), host, service);
	return -1;
    }
    if (socketpair(AF_UNIX,SOCK_STREAM,0,fds))
    {
	report(stderr, GT_("fetchmail: socketpair failed\n"));
	free_plugindata(argvec);
	return -1;
    }
    switch (fork()) {
	case -1:
		/* error */
		free_plugindata(argvec);
		report(stderr, GT_("fetchmail: fork failed\n"));
		return -1;
	case 0:	/* child */
		/* fds[1] is the parent's end; close it for proper EOF
		** detection */
		(void) close(fds[1]);
		if ( (dup2(fds[0],0) == -1) || (dup2(fds[0],1) == -1) ) {
			report(stderr, GT_("dup2 failed\n"));
			_exit(EXIT_FAILURE);
		}
		/* fds[0] is now connected to 0 and 1; close it */
		(void) close(fds[0]);
		if (outlevel >= O_VERBOSE)
		    report(stderr, GT_("running %s (host %s service %s)\n"), plugin, host, service);
		execvp(*argvec, argvec);
		report(stderr, GT_("execvp(%s) failed\n"), *argvec);
		_exit(EXIT_FAILURE);
		break;
	default:	/* parent */
		free_plugindata(argvec);
		break;
    }
    /* fds[0] is the child's end; close it for proper EOF detection */
    (void) close(fds[0]);
    return fds[1];
}

/** Set socket to SO_KEEPALIVE. \return 0 for success. */
static int SockKeepalive(int sock) {
    int keepalive = 1;
    return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof keepalive);
}

int UnixOpen(const char *path)
{
    int sock = -1;
    struct sockaddr_un ad;
    memset(&ad, 0, sizeof(ad));
    ad.sun_family = AF_UNIX;
    strlcpy(ad.sun_path, path, sizeof(ad.sun_path));

    sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if (sock < 0)
    {
	return -1;
    }

    /* Socket opened saved. Useful if connect timeout
     * because it can be closed.
     */
    mailserver_socket_temp = sock;

    if (connect(sock, (struct sockaddr *) &ad, sizeof(ad)) < 0)
    {
	int olderr = errno;
	fm_close(sock);	/* don't use SockClose, no traffic yet */
	errno = olderr;
	sock = -1;
    }

    /* No connect timeout, then no need to set mailserver_socket_temp */
    mailserver_socket_temp = -1;

    return sock;
}

int SockOpen(const char *host, const char *service,
	     const char *plugin, struct addrinfo **ai_in)
{
    struct addrinfo *ai, req;
    int i, acterr = 0;
    int ord;
    char errbuf[8192] = "";

    if (plugin)
	return handle_plugin(host,service,plugin);

    memset(&req, 0, sizeof(struct addrinfo));
    req.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
    req.ai_flags = AI_ADDRCONFIG;
#endif

    i = fm_getaddrinfo(host, service, &req, ai_in);
    if (i) {
	report(stderr, GT_("getaddrinfo(\"%s\",\"%s\") error: %s\n"),
		host, service, gai_strerror(i));
	if (i == EAI_SERVICE)
	    report(stderr, GT_("Try adding the --service option (see also FAQ item R12).\n"));
	return -1;
    }

    /* NOTE a Linux bug here - getaddrinfo will happily return 127.0.0.1
     * twice if no IPv6 is configured */
    i = -1;
    for (ord = 0, ai = *ai_in; ai; ord++, ai = ai->ai_next) {
	char buf[256]; /* hostname */
	char pb[256];  /* service name */
	int gnie;      /* getnameinfo result code */

	gnie = getnameinfo(ai->ai_addr, ai->ai_addrlen, buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
	if (gnie)
	    snprintf(buf, sizeof(buf), GT_("unknown (%s)"), gai_strerror(gnie));
	gnie = getnameinfo(ai->ai_addr, ai->ai_addrlen, NULL, 0, pb, sizeof(pb), NI_NUMERICSERV);
	if (gnie)
	    snprintf(pb, sizeof(pb), GT_("unknown (%s)"), gai_strerror(gnie));

	if (outlevel >= O_VERBOSE)
	    report_build(stdout, GT_("Trying to connect to %s/%s..."), buf, pb);
	i = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (i < 0) {
	    int e = errno;
	    /* mask EAFNOSUPPORT errors, they confuse users for
	     * multihomed hosts */
	    if (errno != EAFNOSUPPORT)
		acterr = errno;
	    if (outlevel >= O_VERBOSE)
		report_complete(stdout, GT_("cannot create socket: %s\n"), strerror(e));
	    snprintf(errbuf+strlen(errbuf), sizeof(errbuf)-strlen(errbuf),\
		     GT_("name %d: cannot create socket family %d type %d: %s\n"), ord, ai->ai_family, ai->ai_socktype, strerror(e));
	    continue;
	}

	SockKeepalive(i);

	/* Save socket descriptor.
	 * Used to close the socket after connect timeout. */
	mailserver_socket_temp = i;

	if (connect(i, (struct sockaddr *) ai->ai_addr, ai->ai_addrlen) < 0) {
	    int e = errno;

	    /* additionally, suppress IPv4 network unreach errors */
	    if (e != EAFNOSUPPORT)
		acterr = errno;

	    if (outlevel >= O_VERBOSE) {
		report_complete(stdout, GT_("connection failed.\n"));
		report(stderr, GT_("connection to %s:%s [%s/%s] failed: %s.\n"), host, service, buf, pb, strerror(e));
	    }
	    snprintf(errbuf+strlen(errbuf), sizeof(errbuf)-strlen(errbuf), GT_("name %d: connection to %s:%s [%s/%s] failed: %s.\n"), ord, host, service, buf, pb, strerror(e));
	    fm_close(i);
	    i = -1;
	    continue;
	} else {
	    if (outlevel >= O_VERBOSE)
		report_complete(stdout, GT_("connected.\n"));
	}

	/* No connect timeout, then no need to set mailserver_socket_temp */
	mailserver_socket_temp = -1;

	break;
    }

    fm_freeaddrinfo(*ai_in);
    *ai_in = NULL;

    if (i == -1) {
	report(stderr, GT_("Connection errors for this poll:\n%s"), errbuf);
	errno = acterr;
    }

    return i;
}


int SockPrintf(int sock, const char* format, ...)
{
    va_list ap;
    char buf[8192];

    va_start(ap, format) ;
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    return SockWrite(sock, buf, strlen(buf));
}

#ifdef SSL_ENABLE
/* OPENSSL_NO_SSL_INTERN: 
   transitional feature for OpenSSL 1.0.1 up to and excluding 1.1.0 
   to make sure we do not access internal structures! */
#define OPENSSL_NO_SSL_INTERN 1
#define OPENSSL_API_COMPAT 10101 // specify API compat level
/* OPENSSL_NO_SSL_INTERN: 
   transitional feature for OpenSSL 1.0.1 up to and excluding 1.1.0 
   to make sure we do not access internal structures! */
#define OPENSSL_NO_SSL_INTERN 1
#define OPENSSL_NO_DEPRECATED 23
#include "tls-aux.h"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>

enum { SSL_min_security_level = 2 };

#ifdef LIBRESSL_VERSION_NUMBER 
# ifdef __OpenBSD__
#  pragma message "WARNING - Linking against LibreSSL, which is not a supported configuration."
# else
#  error "FAILED - LibreSSL cannot be used legally, for lack of GPL clause 2b exception, see COPYING." 
# endif
#endif

#ifdef USING_WOLFSSL
# if LIBWOLFSSL_VERSION_HEX < 0x05002000L
#  error "FAILED - wolfSSL MUST be at least version 5.2.0."
# endif
#else /* !USING_WOLFSSL */
#define fm_MIN_OPENSSL_VER 0x1010100fL
# if OPENSSL_VERSION_NUMBER <  0x101010efL
#  pragma message "WARNING - OpenSSL 1.m.nx SHOULD be at least release version 1.1.1n, using " OPENSSL_VERSION_TEXT "."
# endif                     /* 0xMNN00PPSL */
# if OPENSSL_VERSION_NUMBER >= 0x30000000L
#  if OPENSSL_VERSION_NUMBER < 0x30000020L
#   pragma message "WARNING - OpenSSL 3.m.n SHOULD be at least release version 3.0.2, using " OPENSSL_VERSION_TEXT "."
#  endif
# endif                     /* 0xMNN00PPSL */
# if OPENSSL_VERSION_NUMBER < fm_MIN_OPENSSL_VER
#  error Your OpenSSL version must be at least 1.1.1 release. Older OpenSSL versions are unsupported.
# else /* OpenSSL too old */
/*
#define __fm_ossl_ver(x) #x
#define _fm_ossl_ver(x) __fm_ossl_ver(x)
#pragma message "Building with OpenSSL headers version " _fm_ossl_ver(OPENSSL_VERSION_NUMBER) ", " OPENSSL_VERSION_TEXT
*/
# endif /* OpenSSL too old */
#endif /* USING_WOLFSSL */

static void report_SSL_errors(FILE *stream)
{
    unsigned long err;

    while (0ul != (err = ERR_get_error())) {
	char *errstr = ERR_error_string(err, NULL);
	report(stream, GT_("OpenSSL reported: %s\n"), errstr);
    }
}

/* override ERR_print_errors_fp to our own implementation */
#undef ERR_print_errors_fp
#define ERR_print_errors_fp(stream) report_SSL_errors((stream))

static	SSL_CTX *_ctx[FD_SETSIZE];
static	SSL *_ssl_context[FD_SETSIZE];

static SSL	*SSLGetContext( int );
#endif /* SSL_ENABLE */

int SockWrite(int sock, const char *buf, int len)
{
    int n, wrlen = 0;
#ifdef	SSL_ENABLE
    SSL *ssl = SSLGetContext(sock);
#endif

    while (len)
    {
#ifdef SSL_ENABLE
	if (ssl)
	    n = SSL_write(ssl, buf, len);
	else
#endif /* SSL_ENABLE */
	    n = fm_write(sock, buf, len);
        if (n <= 0)
            return -1;
        len -= n;
	wrlen += n;
	buf += n;
    }
    return wrlen;
}

int SockRead(int sock, char *buf, int len)
{
    char *newline, *bp = buf;
    int n;
#ifdef	SSL_ENABLE
    SSL *ssl;
#endif

    if (--len < 1)
	return(-1);
    do {
	/* 
	 * The reason for these gymnastics is that we want two things:
	 * (1) to read \n-terminated lines,
	 * (2) to return the true length of data read, even if the
	 *     data coming in has embedded NULS.
	 */
#ifdef	SSL_ENABLE
	if( NULL != ( ssl = SSLGetContext( sock ) ) ) {
		int e;
		/* Hack alert! */
		/* XXX FIXME: once we deprecate OpenSSL before 1.1.1, we can 
		 * use SSL_peek_ex() and SSL_read_ex() and simplify this code 
		 * quite a bit */
		/* OK...  SSL_peek works a little different from MSG_PEEK
			Problem is that SSL_peek can return 0 if there
			is no data currently available.  If, on the other
			hand, we lose the socket, we also get a zero, but
			the SSL_read then SEGFAULTS!  To deal with this,
			we'll check the error code any time we get a return
			of zero from SSL_peek.  If we have an error, we bail.
			If we don't, we read one character in SSL_read and
			loop.  This should continue to work even if they
			later change the behavior of SSL_peek
			to "fix" this problem...  :-(	*/
		if ((n = SSL_peek(ssl, bp, len)) <= 0) {
			/* SSL_peek says no data...  Does he mean no data
			or did the connection blow up?  If we got an error
			then bail! */
			e = SSL_get_error(ssl, n);
			if (SSL_ERROR_NONE != e
#ifdef USING_WOLFSSL
			/* wolfSSL 5.0.0 may return SSL_ERROR_WANT_READ when 
			 * receiving HANDSHAKE instead of app data on SSL_peek
			 * https://github.com/wolfSSL/wolfssl/issues/4593 */
					&& SSL_ERROR_WANT_READ != e
#endif
			   )
			{
				ERR_print_errors_fp(stderr);
				return -1;
			}
			/* We didn't get an error so read at least one
				character at this point and loop */
			n = 1;
			/* Make sure newline start out NULL!
			 * We don't have a string to pass through
			 * the strchr at this point yet */
			newline = NULL;
		} else if ((newline = (char *)memchr(bp, '\n', n)) != NULL)
			n = newline - bp + 1;
		/* Matthias Andree: SSL_read can return 0, in that case
		 * we must call SSL_get_error to figure if there was
		 * an error or just a "no data" condition */
		if ((n = SSL_read(ssl, bp, n)) <= 0) {
			e = SSL_get_error(ssl, n);
			if (SSL_ERROR_NONE != e) {
				ERR_print_errors_fp(stderr);
				return -1;
			}
		}
		/* Check for case where our single character turned out to
		 * be a newline...  (It wasn't going to get caught by
		 * the strchr above if it came from the hack...  ). */
		if( NULL == newline && 1 == n && '\n' == *bp ) {
			/* Got our newline - this will break
				out of the loop now */
			newline = bp;
		}
	}
	else
#endif /* SSL_ENABLE */
	{

	    if ((n = fm_peek(sock, bp, len)) <= 0)
		return (-1);
	    if ((newline = (char *)memchr(bp, '\n', n)) != NULL)
		n = newline - bp + 1;
	    if ((n = fm_read(sock, bp, n)) == -1)
		return(-1);
	}
	bp += n;
	len -= n;
    } while 
	    (!newline && len);
    *bp = '\0';

    return bp - buf;
}

int SockPeek(int sock)
/* peek at the next socket character without actually reading it */
{
    int n;
    char ch;
#ifdef	SSL_ENABLE
    SSL *ssl = SSLGetContext(sock);
#endif

#ifdef	SSL_ENABLE
	if (ssl) {
		n = SSL_peek(ssl, &ch, 1);
		if (n <= 0) {
			/* SSL_peek says 0...  Does that mean no data
			or did the connection blow up?  If we got an error
			then bail! */
			int e = SSL_get_error(ssl, n);
			if (SSL_ERROR_NONE != e) {
				ERR_print_errors_fp(stderr);
				return -1;
			}

			/* Haven't seen this case actually occur, but...
			   if the problem in SockRead can occur, this should
			   be possible...  Just not sure what to do here.
			   This should be a safe "punt" the "peek" but don't
			   "punt" the "session"... */

			return 0;	/* Give him a '\0' character */
		}
	}
	else
#endif /* SSL_ENABLE */
	    n = fm_peek(sock, &ch, 1);
	if (n == -1)
		return -1;

    return(ch);
}

#ifdef SSL_ENABLE

struct ssl_callback_data {
    char *ssl_server_cname;
    char *check_digest;
    char *server_label;
    int check_fp;
    int depth0ck;
    int firstrun;
    int prev_err;
    int verify_ok;
    int strict_mode;
};

typedef struct ssl_callback_data t_ssl_callback_data;

SSL *SSLGetContext( int sock )
{
	if( sock < 0 || (unsigned)sock > FD_SETSIZE )
		return NULL;
	if( _ctx[sock] == NULL )
		return NULL;
	return _ssl_context[sock];
}

typedef struct s_digest {
	const EVP_MD *(*digest_algo)(void);
	const char  *digest_name;
} t_digest;

static t_digest digests[] = {
		{ EVP_md5, "MD5" },
		{ EVP_sha1, "SHA1"}
};

/** This function calculates a message digest for the certificate in \a x509_cert with the
 * digest algorithm \a algo_name, and puts the output in a text buffer \a text that has a capacity of \a sz_text bytes.
 * \return
 * - 0 for error (which will already have been reported) - FIXME: invert this?
 * - 1 for success
 */
static int getdigest(const X509 *x509_cert, const char *algo_name, char *text, size_t sz_text)
{
	char *tp, *te;
	unsigned char digest[EVP_MAX_MD_SIZE];
	const EVP_MD *algo = NULL;
	unsigned int dsz;
	int esz;

	/* find algorithm */
	for (unsigned int i = 0; i < countof(digests); i++) {
		if (0 == strcasecmp(digests[i].digest_name, algo_name)) {
			algo = digests[i].digest_algo();
			break;
		}
	}

	if (!algo) {
		report(stderr, GT_("Unknown digest algorithm %s requested.\n"), algo_name);
		return 0;
	}

	/* calculate, binary output to digest, and size in dsz */
	if (!X509_digest(x509_cert, algo, digest, &dsz)) {
		report(stderr, GT_("Out of memory in %s:%lu!\n"), __FILE__, (unsigned long)__LINE__);
		return 0;
	}

	/* format to colon-separated list of hex strings */
	tp = text;
	te = text + sz_text;

	for (size_t dp = 0; dp < dsz; dp++) {
		esz = snprintf(tp, te - tp, dp > 0 ? ":%02X" : "%02X", digest[dp]);
		if (esz >= (te - tp)) {
			report(stderr, GT_("Digest text buffer too small for %s!\n"), algo_name);
			return 0;
		}
		tp += esz;
	}
	return 1;
}

/* ok_return is 1 if this stage of certificate verification
   passed, or 0 if it failed. This callback lets us display informative
   errors, and perform additional validation (e.g. CN matches) */
static int SSL_verify_callback(int ok_return, X509_STORE_CTX *ctx)
{
#define SSLverbose (((outlevel) >= O_DEBUG) || ((outlevel) >= O_VERBOSE && (depth) == 0)) 
	char buf[257];
	X509 *x509_cert;
	int err, depth, i;
	char text[EVP_MAX_MD_SIZE * 3 + 1];
	X509_NAME *subj, *issuer;
	char *tt;
	t_ssl_callback_data *mydata;
	SSL *ssl;

	ssl = (SSL *)X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	mydata = (t_ssl_callback_data *)SSL_get_ex_data(ssl, global_mydata_index);
	x509_cert = X509_STORE_CTX_get_current_cert(ctx);
	err = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);

	subj = X509_get_subject_name(x509_cert);
	issuer = X509_get_issuer_name(x509_cert);

	if (outlevel >= O_DEBUG) {
		if (SSLverbose)
			report(stdout, GT_("SSL verify callback depth %d: verify_ok == %d, err = %d, %s\n"),
					depth, ok_return, err, X509_verify_cert_error_string(err));
	}

	if (outlevel >= O_VERBOSE) {
		if (depth == 0 && SSLverbose)
			report(stdout, GT_("Server certificate:\n"));
		else {
			if (mydata->firstrun) {
				mydata->firstrun = 0;
				if (SSLverbose)
					report(stdout, GT_("Certificate chain, from root to peer, starting at depth %d:\n"), depth);
			} else {
				if (SSLverbose)
					report(stdout, GT_("Certificate at depth %d:\n"), depth);
			}
		}

		if (SSLverbose) {
			if ((i = X509_NAME_get_text_by_NID(issuer, NID_organizationName, buf, sizeof(buf))) != -1) {
				report(stdout, GT_("Issuer Organization: %s\n"), (tt = sdump(buf, i)));
				xfree(tt);
				if ((size_t)i >= sizeof(buf) - 1)
					report(stdout, GT_("Warning: Issuer Organization Name too long (possibly truncated).\n"));
			} else
				report(stdout, GT_("Unknown Organization\n"));
			if ((i = X509_NAME_get_text_by_NID(issuer, NID_commonName, buf, sizeof(buf))) != -1) {
				report(stdout, GT_("Issuer CommonName: %s\n"), (tt = sdump(buf, i)));
				xfree(tt);
				if ((size_t)i >= sizeof(buf) - 1)
					report(stdout, GT_("Warning: Issuer CommonName too long (possibly truncated).\n"));
			} else
				report(stdout, GT_("Unknown Issuer CommonName\n"));
		}
	}

	if ((i = X509_NAME_get_text_by_NID(subj, NID_commonName, buf, sizeof(buf))) != -1) {
		if (SSLverbose) {
			report(stdout, GT_("Subject CommonName: %s\n"), (tt = sdump(buf, i)));
			xfree(tt);
		}
		if ((size_t)i >= sizeof(buf) - 1) {
			/* Possible truncation. In this case, this is a DNS name, so this
			 * is really bad. We do not tolerate this even in the non-strict case. */
			report(stderr, GT_("Bad certificate: Subject CommonName too long!\n"));
			return (0);
		}
		if ((size_t)i > strlen(buf)) {
			/* Name contains embedded NUL characters, so we complain. This is likely
			 * a certificate spoofing attack. */
			report(stderr, GT_("Bad certificate: Subject CommonName contains NUL, aborting!\n"));
			return 0;
		}
	}

	if (depth == 0) { /* peer certificate */
		if (!mydata->depth0ck) {
			mydata->depth0ck = 1;
		}

		if ((i = X509_NAME_get_text_by_NID(subj, NID_commonName, buf, sizeof(buf))) != -1) {
			if (mydata->ssl_server_cname != NULL) {
				char *p1 = buf;
				char *p2 = mydata->ssl_server_cname;
				int matched = 0;
				STACK_OF(GENERAL_NAME) *gens;

				/* RFC 2595 section 2.4: find a matching name
				 * first find a match among alternative names */
				gens = (STACK_OF(GENERAL_NAME) *)X509_get_ext_d2i(x509_cert, NID_subject_alt_name, NULL, NULL);
				if (gens) {
					int j, r;
					for (j = 0, r = sk_GENERAL_NAME_num(gens); j < r; ++j) {
						const GENERAL_NAME *gn = sk_GENERAL_NAME_value(gens, j);
						if (gn->type == GEN_DNS) {
							char *pp1 = (char *)gn->d.ia5->data;
							char *pp2 = mydata->ssl_server_cname;
							if (outlevel >= O_VERBOSE) {
								report(stdout, GT_("Subject Alternative Name: %s\n"), (tt = sdump(pp1, (size_t)gn->d.ia5->length)));
								xfree(tt);
							}
							/* Name contains embedded NUL characters, so we complain. This
							 * is likely a certificate spoofing attack. */
							if ((size_t)gn->d.ia5->length != strlen(pp1)) {
								report(stderr, GT_("Bad certificate: Subject Alternative Name contains NUL, aborting!\n"));
								sk_GENERAL_NAME_free(gens);
								return 0;
							}
							if (name_match(pp1, pp2)) {
							    matched = 1;
							}
						}
					}
					GENERAL_NAMES_free(gens);
				}
				if (name_match(p1, p2)) {
					matched = 1;
				}
				if (!matched) {
					if (mydata->strict_mode || SSLverbose) {
						report(stderr,
								GT_("Server CommonName mismatch: %s != %s\n"),
								(tt = sdump(buf, i)), mydata->ssl_server_cname);
						xfree(tt);
					}
					ok_return = 0;
				}
			} else if (ok_return) {
				report(stderr, GT_("Server name not set, could not verify certificate!\n"));
				if (mydata->strict_mode) return (0);
			}
		} else {
			if (outlevel >= O_VERBOSE)
				report(stdout, GT_("Unknown Server CommonName\n"));
			if (ok_return && mydata->strict_mode) {
				report(stderr, GT_("Server name not specified in certificate!\n"));
				return (0);
			}
		}
		/* Print the finger print. Note that on errors, we might print it more than once
		 * normally; we kluge around that by using a global variable. */
		if (1 == mydata->check_fp) {
			const char *algo = "MD5";
			mydata->check_fp = -1;
			if (!getdigest(x509_cert, algo, text, sizeof(text))) {
				return 0;
			}
			if (outlevel > O_NORMAL) {
			    const char *algo_sha1 = "SHA1"; /* fixme: support {SHA1} syntax; fixme: support list of fingerprints */
			    report(stdout, GT_("%s certificate %s fingerprint: %s\n"), mydata->server_label, algo, text);
			    char text_sha1[EVP_MAX_MD_SIZE * 3 + 1];
			    if (getdigest(x509_cert, algo_sha1, text_sha1, sizeof(text_sha1))) {
				report(stdout, GT_("%s certificate %s fingerprint: %s\n"), mydata->server_label, algo_sha1, text_sha1);
			}
				}
			if (mydata->check_digest != NULL) {
				if (strcasecmp(text, mydata->check_digest) == 0) {
			if (outlevel > O_NORMAL)
					report(stdout, GT_("%s fingerprints match.\n"), mydata->server_label);
				} else {
				    report(stderr, GT_("%s fingerprints do not match!\n"), mydata->server_label);
				    return (0);
				}
			} /* if (_check_digest != NULL) */
		} /* if (_check_fp) */
	} /* if (depth == 0 && !_depth0ck) */

	if (err != X509_V_OK && err != mydata->prev_err && !(mydata->check_fp != 0 && mydata->check_digest && !mydata->strict_mode)) {
		char *tmp;
		int did_rep_err = 0;
		mydata->prev_err = err;

                report(stderr, GT_("Server certificate verification error: %s\n"), X509_verify_cert_error_string(err));
                /* We gave the error code, but maybe we can add some more details for debugging */

		switch (err) {
		/* actually we do not want to lump these together, but
		 * since OpenSSL flipped the meaning of these error
		 * codes in the past, and they do hardly make a
		 * practical difference because servers need not provide
		 * the root signing certificate, we don't bother telling
		 * users the difference:
		 */
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
			X509_NAME_oneline(issuer, buf, sizeof(buf));
			buf[sizeof(buf) - 1] = '\0';
			report(stderr, GT_("Broken certification chain at: %s\n"), (tmp = sdump(buf, strlen(buf))));
			xfree(tmp);
			report(stderr, GT_(	"This could mean that the server did not provide the intermediate CA's certificate(s), "
						"which is nothing fetchmail could do anything about.  For details, "
						"please see the README.SSL-SERVER document that ships with fetchmail.\n"));
			did_rep_err = 1;
			/* FALLTHROUGH */
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
			if (!did_rep_err) {
			    X509_NAME_oneline(issuer, buf, sizeof(buf));
			    buf[sizeof(buf) - 1] = '\0';
			    report(stderr, GT_("Missing trust anchor certificate: %s\n"), (tmp = sdump(buf, strlen(buf))));
			    xfree(tmp);
			}
			report(stderr, GT_(	"This could mean that the root CA's signing certificate is not in the "
						"trusted CA certificate location, or that c_rehash needs to be run "
						"on the certificate directory. For details, please "
						"see the documentation of --sslcertpath and --sslcertfile in the manual page. "
						"See README.SSL for details.\n"));
			break;
		default:
			break;
		}
	}
	/*
	 * If not in strict checking mode (--sslcertck), override this
	 * and pretend that verification had succeeded.
	 */
	mydata->verify_ok &= ok_return;
	if (!mydata->strict_mode)
		ok_return = 1;
	return ok_return;
}

/* get commonName from certificate set in file.
 * commonName is stored in buffer namebuffer, limited with namebufferlen
 */
static const char *SSLCertGetCN(const char *mycert,
                                char *namebuffer, size_t namebufferlen)
{
	const char *ret       = NULL;
	BIO        *certBio   = NULL;
	X509       *x509_cert = NULL;
	X509_NAME  *certname  = NULL;

	if (namebuffer && namebufferlen > 0) {
		namebuffer[0] = 0x00;
		certBio = BIO_new_file(mycert,"r");
		if (certBio) {
			x509_cert = PEM_read_bio_X509(certBio,NULL,NULL,NULL);
			BIO_free(certBio);
		}
		if (x509_cert) {
			certname = X509_get_subject_name(x509_cert);
			if (certname &&
			    X509_NAME_get_text_by_NID(certname, NID_commonName,
						      namebuffer, namebufferlen) > 0)
				ret = namebuffer;
			X509_free(x509_cert);
		}
	}
	return ret;
}

/* implementation for OpenSSL 1.1.x and newer */
// OpenSSL 3.0.0 may require 0 instead of TLS_MAX_VERSION. It is documented for 
// 1.1.1 already as "automatic max version" and the macro will be removed:
#if !HAVE_DECL_TLS_MAX_VERSION && !defined(TLS_MAX_VERSION)
#define TLS_MAX_VERSION 0
#endif

static int OSSL_proto_version_logic(int sock, const char **myproto)
{
	/* NOTE - this code MUST NOT set myproto to NULL, else the
	 * SSL_...set_..._proto_version() call becomes ineffective. */
	_ctx[sock] = SSL_CTX_new(TLS_client_method());
	// In line with RFC 7525, default to TLSv1.2+
	SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);

	if (!*myproto) {
	    *myproto = "auto";
	}

	if        (!strcasecmp("tls1", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS1_VERSION);
	} else if (!strcasecmp("tls1+", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);
	// undocumented convenience feature:
	} else if (!strcasecmp("tls1.0", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS1_VERSION);
	// undocumented convenience feature:
	} else if (!strcasecmp("tls1.0+", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);
	} else if (!strcasecmp("tls1.1", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS1_1_VERSION);
	} else if (!strcasecmp("tls1.1+", *myproto)) { // RFC 7525 SHOULD NOT
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_1_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);
	} else if (!strcasecmp("tls1.2", *myproto)) { // DISCOURAGED
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_2_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS1_2_VERSION);
	} else if (!strcasecmp("tls1.2+", *myproto)) {
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_2_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);
	} else if (!strcasecmp("tls1.3", *myproto)) {
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_3_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS1_3_VERSION);
	} else if (!strcasecmp("tls1.3+", *myproto)) {
		SSL_CTX_set_min_proto_version(_ctx[sock], TLS1_3_VERSION);
		SSL_CTX_set_max_proto_version(_ctx[sock], TLS_MAX_VERSION);
	} else if (!strcasecmp("ssl23", *myproto)
	        || 0 == strcasecmp("tls", *myproto)
	        || 0 == strcasecmp("auto", *myproto))
	{
		/* do nothing, default was set before the if/elseif block */
	} else {
		/* This should not happen. */
		report(stderr,
		        GT_("Invalid SSL protocol '%s' specified, using default autoselect (auto).\n"),
		        *myproto);
	}
	return 0;
}

/* flush (discard) pending input from socket */
void inputflush(int sock) {
	char buf[1024];
	int s;

	while((ioctl(sock, FIONREAD, &s) >= 0) && s > 0) {
		if (recv(sock, buf, sizeof buf, MSG_DONTWAIT) <= 0)
			break;
	}
}

#ifdef	SSL_ENABLE
static void fm_SSLCleanup(int sock) {
    if( NULL != SSLGetContext( sock ) ) {
        /* Clean up the SSL stack */
        SSL_shutdown( _ssl_context[sock] );
        SSL_free( _ssl_context[sock] );
        _ssl_context[sock] = NULL;
	SSL_CTX_free(_ctx[sock]);
	_ctx[sock] = NULL;
    }
}
#endif

/* performs initial SSL handshake over the connected socket
 * uses SSL *ssl global variable, which is currently defined
 * in this file
 */
int SSLOpen(int sock, char *mycert, char *mykey, const char *myproto, int certck,
    char *cacertfile, char *certpath,
    char *fingerprint, char *servercname, char *label, char **remotename)
{
        struct stat randstat;
        int i;
	long sslopts = SSL_OP_ALL;

	static int ssl_lib_init = 0;

#ifdef USING_WOLFSSL
	{ char *tmp;
	    if (NULL != (tmp = getenv("FETCHMAIL_WOLFSSL_DEBUG"))) {
		if (*tmp) wolfSSL_Debugging_ON();
	    }
	}
#endif

	{
	    static long ssl_lib_version;

	    if (!ssl_lib_init) {
		ssl_lib_version = OpenSSL_version_num();
		ssl_lib_init = 1;
	    }

	    if (ssl_lib_version < OPENSSL_VERSION_NUMBER) {
		report(stderr, GT_("Loaded OpenSSL library %#lx older than headers %#lx, refusing to work.\n"), (long)ssl_lib_version, (long)(OPENSSL_VERSION_NUMBER));
	    return -1;
	}

	    if (ssl_lib_version > OPENSSL_VERSION_NUMBER && outlevel >= O_VERBOSE) {
		report(stdout, GT_("Loaded OpenSSL library %#lx newer than headers %#lx, trying to continue.\n"), (long)ssl_lib_version, (long)(OPENSSL_VERSION_NUMBER));
	    }
	}
	
	if (-2 == global_mydata_index) {
	    char tmp[] = "fetchmail SSL callback data";
	    global_mydata_index = SSL_get_ex_new_index(0, tmp, NULL, NULL, NULL);
	    if (-1 == global_mydata_index) return PS_UNDEFINED;
	}

        if (stat("/dev/random", &randstat)  &&
            stat("/dev/urandom", &randstat)) {
          /* Neither /dev/random nor /dev/urandom are present, so add
             entropy to the SSL PRNG a hard way. */
          for (i = 0; i < 10000  &&  ! RAND_status (); ++i) {
            char buf[4];
            struct timeval tv;
            gettimeofday (&tv, 0);
            buf[0] = tv.tv_usec & 0xF;
            buf[2] = (tv.tv_usec & 0xF0) >> 4;
            buf[3] = (tv.tv_usec & 0xF00) >> 8;
            buf[1] = (tv.tv_usec & 0xF000) >> 12;
            RAND_add (buf, sizeof buf, 0.1);
          }
        }

	if( sock < 0 || (unsigned)sock > FD_SETSIZE ) {
		report(stderr, GT_("File descriptor out of range for SSL") );
		return( -1 );
	}

	/* Make sure a connection referring to an older context is not left */
	_ssl_context[sock] = NULL;
	{
		int rc = OSSL_proto_version_logic(sock, &myproto);
		if (rc) return rc;
	}
	/* do not combine into an else { } as myproto may be nulled above! */
	if (!myproto) {
		/* SSLv23 is a misnomer and will in fact use the best
		 available protocol, subject to SSL_OP_NO* constraints. */
		_ctx[sock] = SSL_CTX_new(SSLv23_client_method());
	}

	if(_ctx[sock] == NULL) {
		unsigned long ec = ERR_peek_last_error();
		ERR_print_errors_fp(stderr);
#ifdef SSL_R_NULL_SSL_METHOD_PASSED /* wolfSSL does not define this error */
		if (ERR_GET_REASON(ec) == SSL_R_NULL_SSL_METHOD_PASSED) {
		    report(stderr, GT_("Note that some distributions disable older protocol versions in weird non-standard ways. Try a newer protocol version.\n"));
		}
#endif
		return(-1);
	}

	{
	    char *tmp = getenv("FETCHMAIL_DISABLE_CBC_IV_COUNTERMEASURE");
	    if (tmp == NULL || *tmp == '\0' || strspn(tmp, " \t") == strlen(tmp))
		sslopts &= ~ SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;
	}

	(void)SSL_CTX_set_options(_ctx[sock], sslopts | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

	(void)SSL_CTX_set_mode(_ctx[sock], SSL_MODE_AUTO_RETRY);

	SSL_CTX_set_verify(_ctx[sock], SSL_VERIFY_PEER, SSL_verify_callback);

	/* Check which trusted X.509 CA certificate store(s) to load */
	{
		char *tmp;
		int want_default_cacerts = 0;
		int r = 1;
		const char *l1 = 0, *l2 = 0;

		/* Load user locations if any is given */
		if (certpath || cacertfile) {
			l1 = cacertfile;
			l2 = certpath;
			r = SSL_CTX_load_verify_locations(_ctx[sock],
						cacertfile, certpath);
			if (1 != r) goto no_verify_load;
		} else {
			want_default_cacerts = 1;
		}

		tmp = getenv("FETCHMAIL_INCLUDE_DEFAULT_X509_CA_CERTS");
		if (want_default_cacerts || (tmp && tmp[0])) {
#ifdef USING_WOLFSSL
			/* wolfSSL 5.0.0 does not implement
			 * SSL_CTX_set_default_verify_paths(). Use something
			 * else: */
			const char *tmp = WOLFSSL_TRUST_FILE;
			l1 = tmp; l2=NULL;
			if (*tmp)
				r = SSL_CTX_load_verify_locations(_ctx[sock],
						tmp, NULL);
#else
			r = SSL_CTX_set_default_verify_paths(_ctx[sock]);
			if (1 != r) goto no_verify_load;
#endif
		}

		if (1 != r) {
no_verify_load:
			report(stderr, GT_("Cannot load verify locations (file=\"%s\", dir=\"%s\"), error %d:\n"),
					l1?l1:"(null)", l2?l2:"(null)", r);
			ERR_print_errors_fp(stderr);
			return -1;
		}
	}
	
	_ssl_context[sock] = SSL_new(_ctx[sock]);
	
	if(_ssl_context[sock] == NULL) {
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(_ctx[sock]);
		_ctx[sock] = NULL;
		return(-1);
	}
	
	// DEBUG: dump ordered SSL cipher list, client side
	if (outlevel >= O_DEBUG) {
	    const char *tmp;
	    for (int i = 0; (tmp = SSL_get_cipher_list(_ssl_context[sock], i)) != NULL; i++) {
		report(stdout, GT_("SSL/TLS: prio %3d cipher is %s\n"), i, tmp);
	    }
	}

	t_ssl_callback_data mydata;
	memset(&mydata, 0, sizeof(mydata));

	/* This data is for the verify callback */
	mydata.ssl_server_cname = servercname;
	mydata.server_label = label;
	mydata.check_fp = 1;
	mydata.check_digest = fingerprint;
	mydata.depth0ck = 0;
	mydata.firstrun = 1;
	mydata.verify_ok = 1;
	mydata.prev_err = -1;
	mydata.strict_mode = certck;

	/*
	 * Support SNI, some servers (googlemail) appear to require it.
	 */
	{
	    long r;
	    r = SSL_set_tlsext_host_name(_ssl_context[sock], servercname);

	    if (0 == r) {
		/* handle error */
		report(stderr, GT_("Warning: SSL_set_tlsext_host_name(%p, \"%s\") failed (code %#lx), trying to continue.\n"), (void *)_ssl_context[sock], servercname, r);
		ERR_print_errors_fp(stderr);
	    }
	}

#ifdef USING_WOLFSSL
	{
		/* workaround for WolfSSL 5.0.0 compatibility issue,
		 * which leaves errors in the X509 ctx passed to the 
		 * SSL_verify_callback() in a preverify_ok==1 case,
		 * where OpenSSL will not return an error.
		 * https://github.com/wolfSSL/wolfssl/issues/4592 */
		int r = wolfSSL_check_domain_name(_ssl_context[sock], servercname);
		if (WOLFSSL_SUCCESS != r) {
			report(stderr, GT_("fetchmail: sock %d: wolfSSL_check_domain_name(%#p, \"%s\") returned %d, trying to continue\n"), 
					sock, _ssl_context[sock], servercname, r);
		}
	}
#else
	/* set host name for verification, only available since OpenSSL 1.0.2 
	 * */
	/* XXX FIXME: do we need to change the function's signature and pass the akalist to
	 * permit the other hostnames through SSL? */
	/* https://wiki.openssl.org/index.php/Hostname_validation */
	{
	    int r;
	    X509_VERIFY_PARAM *param = SSL_get0_param(_ssl_context[sock]);

	    X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
	    if (0 == (r = X509_VERIFY_PARAM_set1_host(param, servercname, strlen(servercname)))) {
		report(stderr, GT_("Warning: X509_VERIFY_PARAM_set1_host(%p, \"%s\") failed (code %#x), trying to continue.\n"),
			(void *)_ssl_context[sock], servercname, r);
		ERR_print_errors_fp(stderr);
	    }

	    /* param is a pointer to internal OpenSSL data, must not be freed,
	     * and just goes out of scope */
	}
#endif

	if( mycert || mykey ) {

	/* Ok...  He has a certificate file defined, so lets declare it.  If
	 * he does NOT have a separate certificate and private key file then
	 * assume that it's a combined key and certificate file.
	 */
		char buffer[256];
		
		if( !mykey )
			mykey = mycert;
		if( !mycert )
			mycert = mykey;

		if ((!*remotename || !**remotename) && SSLCertGetCN(mycert, buffer, sizeof(buffer))) {
			free(*remotename);
			*remotename = xstrdup(buffer);
		}
		SSL_use_certificate_file(_ssl_context[sock], mycert, SSL_FILETYPE_PEM);
		SSL_use_PrivateKey_file(_ssl_context[sock], mykey, SSL_FILETYPE_PEM);
	}

	SSL_set_ex_data(_ssl_context[sock], global_mydata_index, &mydata);

	int ssle_connect = 0;
	if (SSL_set_fd(_ssl_context[sock], sock) == 0 
	    || (ssle_connect = SSL_connect(_ssl_context[sock])) < 1) {
		int e = errno;
		unsigned long ssle_err_from_get_error = SSL_get_error(_ssl_context[sock], ssle_connect);
		unsigned long ssle_err_from_queue = ERR_peek_error();
		ERR_print_errors_fp(stderr);
		if (SSL_ERROR_SYSCALL == ssle_err_from_get_error && 0 == ssle_err_from_queue) {
		    if (0 == ssle_connect) {
			/* FIXME: the next line was hacked in 6.4.0-rc1 so the translation strings don't change.
			 * The %s could be merged to the inside of GT_(). */
			report(stderr, "%s: %s", servercname, GT_("Server shut down connection prematurely during SSL_connect().\n"));
		    } else if (ssle_connect < 0) {
			report(stderr, "%s: ", servercname);
			report(stderr, GT_("System error during SSL_connect(): %s\n"), e ? strerror(e) : GT_("handshake failed at protocol or connection level."));
		    }
		}
		SSL_free( _ssl_context[sock] );
		_ssl_context[sock] = NULL;
		SSL_CTX_free(_ctx[sock]);
		_ctx[sock] = NULL;
		return(-1);
	}

	if (outlevel >= O_VERBOSE) {
	    SSL_CIPHER const *sc;
	    int bitsmax, bitsused;

	    const char *vers;

	    vers = SSL_get_version(_ssl_context[sock]);

	    sc = SSL_get_current_cipher(_ssl_context[sock]);
	    if (!sc) {
		report (stderr, GT_("Cannot obtain current SSL/TLS cipher - no session established?\n"));
	    } else {
		bitsused = SSL_CIPHER_get_bits(sc, &bitsmax);
		report(stdout, GT_("SSL/TLS: using protocol %s, cipher %s, %d/%d secret/processed bits\n"),
			vers, SSL_CIPHER_get_name(sc), bitsused, bitsmax);
	    }
	}

	/* Paranoia: was the callback not called as we expected? */
	if (!mydata.depth0ck) {
		report(stderr, GT_("Certificate/fingerprint verification was somehow skipped!\n"));

		if (fingerprint != NULL || certck) {
		    fm_SSLCleanup(sock);
		    return -1;
		}
	}

	if (!certck && !fingerprint &&
		(SSL_get_verify_result(_ssl_context[sock]) != X509_V_OK || !mydata.verify_ok)) {
		report(stderr, GT_("Warning: the connection is insecure, continuing anyways. (Better use --sslcertck!)\n"));
	}

	return(0);
}
#endif

int SockClose(int sock)
/* close a socket gracefully */
{
#ifdef	SSL_ENABLE
    fm_SSLCleanup(sock);
#endif

    /* if there's an error closing at this point, not much we can do */
    return(fm_close(sock));	/* this is guarded */
}
