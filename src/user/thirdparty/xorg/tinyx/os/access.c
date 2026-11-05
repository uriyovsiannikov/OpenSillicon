/***********************************************************

Copyright 1987, 1998  The Open Group
Copyright 2004 Sun Microsystems, Inc.

All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, provided that the above
copyright notice(s) and this permission notice appear in all copies of
the Software and that both the above copyright notice(s) and this
permission notice appear in supporting documentation.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL
INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING
FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

Except as contained in this notice, the name of a copyright holder
shall not be used in advertising or otherwise to promote the sale, use
or other dealings in this Software without prior written authorization
of the copyright holder.

X Window System is a trademark of The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#define XSERV_t
#define TRANS_SERVER
#include <X11/Xtrans/Xtrans.h>
#include <X11/Xauth.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "site.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <X11/Xos_r.h>

#include "dixstruct.h"
#include "osdep.h"


#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

Bool defeatAccessControl = FALSE;

#define acmp(a1, a2, len) memcmp((char *)(a1), (char *)(a2), len)
#define acopy(a1, a2, len) memmove((char *)(a2), (char *)(a1), len)
#define addrEqual(fam, address, length, host) \
			 ((fam) == (host)->family &&\
			  (length) == (host)->len &&\
			  !acmp (address, (host)->addr, length))

static int ConvertAddr(struct sockaddr * /*saddr*/,
		       int * /*len*/,
		       pointer * /*addr*/);

static Bool NewHost(int /*family*/,
		    const void * /*addr*/,
		    int /*len*/,
		    int /* addingLocalHosts */);


/* XFree86 bug #156: To keep track of which hosts were explicitly requested in
   /etc/X<display>.hosts, we've added a requested field to the HOST struct,
   and a LocalHostRequested variable.  These default to FALSE, but are set
   to TRUE in ResetHosts when reading in /etc/X<display>.hosts.  They are
   checked in DisableLocalHost(), which is called to disable the default
   local host entries when stronger authentication is turned on. */

typedef struct _host {
	short		family;
	short		len;
	unsigned char	*addr;
	struct _host *next;
	int		requested;
} HOST;

#define MakeHost(h,l)	(h)=malloc(sizeof *(h)+(l));\
			if (h) { \
			   (h)->addr=(unsigned char *) ((h) + 1);\
			   (h)->requested = FALSE; \
			}
#define FreeHost(h)	free(h)
static HOST *selfhosts = NULL;
static HOST *validhosts = NULL;
static int AccessEnabled = DEFAULT_ACCESS_CONTROL;
static int LocalHostEnabled = FALSE;
static int LocalHostRequested = FALSE;

/*
 * called when authorization is not enabled to add the
 * local host to the access list
 */

void
EnableLocalHost (void)
{
    LocalHostEnabled = TRUE;
    AddLocalHosts ();
}

/*
 * called when authorization is enabled to keep us secure
 */
void
DisableLocalHost (void)
{
    HOST *self;

    if (!LocalHostRequested)		/* Fix for XFree86 bug #156 */
	LocalHostEnabled = FALSE;
    for (self = selfhosts; self; self = self->next) {
      if (!self->requested)		/* Fix for XFree86 bug #156 */
	(void) RemoveHost ((ClientPtr)NULL, self->family, self->len, (pointer)self->addr);
    }
}

/*
 */




/*
 * DefineSelf (fd):
 *
 * Define this host for access control.  Find all the hosts the OS knows about
 * for this fd and add them to the selfhosts list.
 */


void
DefineSelf(int fd)
{
	HOST *host;
	(void)fd;
	for (host = selfhosts;
	    host && !addrEqual(FamilyLocalHost, "", 0, host);
	    host = host->next)
		;
	if (host)
		return;
	MakeHost(host, 0);
	if (host) {
		host->family = FamilyLocalHost;
		host->len = 0;
		acopy("", host->addr, 0);
		host->next = selfhosts;
		selfhosts = host;
	}
}


void
AddLocalHosts (void)
{
    HOST    *self;

    for (self = selfhosts; self; self = self->next)
	    /* Fix for XFree86 bug #156: pass addingLocal = TRUE to
	     * NewHost to tell that we are adding the default local
	     * host entries and not to flag the entries as being
	     * explicitely requested */
	(void) NewHost (self->family, self->addr, self->len, TRUE);
}

/* Reset access control list to initial hosts */
void
ResetHosts (const char *display)
{
    HOST	*host;
    char                ohostname[120];
    char		fname[PATH_MAX + 1];
    int			fnamelen;
    FILE		*fd;
    char		*ptr;
    AccessEnabled = defeatAccessControl ? FALSE : DEFAULT_ACCESS_CONTROL;
    LocalHostEnabled = FALSE;
    while ((host = validhosts) != 0)
    {
        validhosts = host->next;
        FreeHost (host);
    }

#define ETC_HOST_PREFIX "/etc/X"
#define ETC_HOST_SUFFIX ".hosts"
    fnamelen = strlen(ETC_HOST_PREFIX) + strlen(ETC_HOST_SUFFIX) +
		strlen(display) + 1;
    if (fnamelen > sizeof(fname))
	FatalError("Display name `%s' is too long\n", display);
    sprintf(fname, ETC_HOST_PREFIX "%s" ETC_HOST_SUFFIX, display);

    if ((fd = fopen (fname, "r")) != 0)
    {
        while (fgets (ohostname, sizeof (ohostname), fd))
	{
	if (*ohostname == '#')
	    continue;
    	if ((ptr = strchr(ohostname, '\n')) != 0)
    	    *ptr = 0;
	if (!strncasecmp("local:", ohostname, 6))
	{
	    NewHost(FamilyLocalHost, "", 0, FALSE);
	    LocalHostRequested = TRUE;	/* Fix for XFree86 bug #156 */
	}
	/* Internet and server-interpreted entries are unsupported without
	 * network transports. */
        }
        fclose (fd);
    }
}

/* Is client on the local host */
_X_EXPORT Bool LocalClient(ClientPtr client)
{
    int    		alen, family, notused;
    Xtransaddr		*from = NULL;
    pointer		addr;
    HOST	*host;

    if (!_XSERVTransGetPeerAddr (((OsCommPtr)client->osPrivate)->trans_conn,
	&notused, &alen, &from))
    {
	family = ConvertAddr ((struct sockaddr *) from,
	    &alen, (pointer *)&addr);
	if (family == -1)
	{
	    free((char *) from);
	    return FALSE;
	}
	if (family == FamilyLocal)
	{
	    free((char *) from);
	    return TRUE;
	}
	for (host = selfhosts; host; host = host->next)
	{
	    if (addrEqual (family, addr, alen, host))
		return TRUE;
	}
	free((char *) from);
    }
    return FALSE;
}

/*
 * Return the uid and gid of a connected local client
 * or the uid/gid for nobody those ids cannot be determined
 *
 * Used for peer credentials in connection audit records.
 */
int
LocalClientCred(ClientPtr client, int *pUid, int *pGid)
{
#if defined(HAS_GETPEEREID) || defined(HAS_GETPEERUCRED) || defined(SO_PEERCRED)
    int fd;
    XtransConnInfo ci;
#ifdef HAS_GETPEEREID
    uid_t uid;
    gid_t gid;
#elif defined(HAS_GETPEERUCRED)
    ucred_t *peercred = NULL;
#elif defined(SO_PEERCRED)
    struct ucred peercred;
    socklen_t so_len = sizeof(peercred);
#endif

    if (client == NULL)
        return -1;
    ci = ((OsCommPtr)client->osPrivate)->trans_conn;
    if (!_XSERVTransIsLocal(ci))
        return -1;

    fd = _XSERVTransGetConnectionNumber(ci);
#ifdef HAS_GETPEEREID
    if (getpeereid(fd, &uid, &gid) == -1)
        return -1;
    if (pUid != NULL)
        *pUid = uid;
    if (pGid != NULL)
        *pGid = gid;
    return 0;
#elif defined(HAS_GETPEERUCRED)
    if (getpeerucred(fd, &peercred) < 0)
        return -1;
    if (pUid != NULL)
        *pUid = ucred_geteuid(peercred);
    if (pGid != NULL)
        *pGid = ucred_getegid(peercred);
    ucred_free(peercred);
    return 0;
#elif defined(SO_PEERCRED)
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peercred, &so_len) == -1)
        return -1;
    if (pUid != NULL)
        *pUid = peercred.uid;
    if (pGid != NULL)
        *pGid = peercred.gid;
    return 0;
#endif
#else
    (void)client;
    (void)pUid;
    (void)pGid;
    return -1;
#endif
}

static Bool
AuthorizedClient(ClientPtr client)
{
    if (!client || defeatAccessControl)
	return TRUE;
    return LocalClient(client);
}

/* Add a host to the access control list.  This is the external interface
 * called from the dispatcher */

int
AddHost (ClientPtr	client,
	 int            family,
	 unsigned       length,        /* of bytes in pAddr */
	 const void *   pAddr)
{
    int			len;

    if (!AuthorizedClient(client))
	return(BadAccess);
    switch (family) {
    case FamilyLocalHost:
	len = length;
	LocalHostEnabled = TRUE;
	break;
    default:
	if (client) client->errorValue = family;
	return (BadValue);
    }
    if (NewHost (family, pAddr, len, FALSE))
	return Success;
    return BadAlloc;
}

/* Add a host to the access control list. This is the internal interface
 * called when starting or resetting the server */
static Bool
NewHost (int		family,
	 const void *	addr,
	 int		len,
	 int		addingLocalHosts)
{
    HOST *host;

    for (host = validhosts; host; host = host->next)
    {
        if (addrEqual (family, addr, len, host))
	    return TRUE;
    }
    if (!addingLocalHosts) {			/* Fix for XFree86 bug #156 */
	for (host = selfhosts; host; host = host->next) {
	    if (addrEqual (family, addr, len, host)) {
		host->requested = TRUE;
		break;
	    }
	}
    }
    MakeHost(host,len)
    if (!host)
	return FALSE;
    host->family = family;
    host->len = len;
    acopy(addr, host->addr, len);
    host->next = validhosts;
    validhosts = host;
    return TRUE;
}

/* Remove a host from the access control list */

int
RemoveHost (
    ClientPtr		client,
    int                 family,
    unsigned            length,        /* of bytes in pAddr */
    pointer             pAddr)
{
    int			len;
    HOST	*host, **prev;

    if (!AuthorizedClient(client))
	return(BadAccess);
    switch (family) {
    case FamilyLocalHost:
	len = length;
	LocalHostEnabled = FALSE;
	break;
    default:
	if (client)
	    client->errorValue = family;
        return(BadValue);
    }
    for (prev = &validhosts;
         (host = *prev) && (!addrEqual (family, pAddr, len, host));
         prev = &host->next)
        ;
    if (host)
    {
        *prev = host->next;
        FreeHost (host);
    }
    return (Success);
}

/* Get all hosts in the access control list */
int
GetHosts (
    pointer		*data,
    int			*pnHosts,
    int			*pLen,
    BOOL		*pEnabled)
{
    int			len;
    int 	n = 0;
    unsigned char *ptr;
    HOST	*host;
    int			nHosts = 0;

    *pEnabled = AccessEnabled ? EnableAccess : DisableAccess;
    for (host = validhosts; host; host = host->next)
    {
	nHosts++;
	n += (((host->len + 3) >> 2) << 2) + sizeof(xHostEntry);
    }
    if (n)
    {
        *data = ptr = malloc(n);
	if (!ptr)
	{
	    return(BadAlloc);
	}
        for (host = validhosts; host; host = host->next)
	{
	    len = host->len;
	    ((xHostEntry *)ptr)->family = host->family;
	    ((xHostEntry *)ptr)->length = len;
	    ptr += sizeof(xHostEntry);
	    acopy (host->addr, ptr, len);
	    ptr += ((len + 3) >> 2) << 2;
        }
    } else {
	*data = NULL;
    }
    *pnHosts = nHosts;
    *pLen = n;
    return(Success);
}

/* Check if a host is not in the access control list.
 * Returns 1 if host is invalid, 0 if we've found it. */

int
InvalidHost (
    register struct sockaddr	*saddr,
    int				len,
    ClientPtr			client)
{
    int 			family;
    pointer			addr = NULL;
    HOST 		*selfhost, *host;

    (void)client;
    if (!AccessEnabled)   /* just let them in */
        return(0);
    family = ConvertAddr (saddr, &len, (pointer *)&addr);
    if (family == -1)
        return 1;
    if (family == FamilyLocal)
    {
	if (!LocalHostEnabled)
 	{
	    /*
	     * check to see if any local address is enabled.  This
	     * implicitly enables local connections.
	     */
	    for (selfhost = selfhosts; selfhost; selfhost=selfhost->next)
 	    {
		for (host = validhosts; host; host=host->next)
		{
		    if (addrEqual (selfhost->family, selfhost->addr,
				   selfhost->len, host))
			return 0;
		}
	    }
	} else
	    return 0;
    }
    for (host = validhosts; host; host = host->next)
    {
	if (addrEqual (family, addr, len, host))
	    return (0);

    }
    return (1);
}

static int
ConvertAddr (
    register struct sockaddr	*saddr,
    int				*len,
    pointer			*addr)
{
    if (*len == 0)
        return (FamilyLocal);
    switch (saddr->sa_family)
    {
    case AF_UNSPEC:
#ifdef UNIXCONN
    case AF_UNIX:
#endif
        return FamilyLocal;
    default:
        return -1;
    }
}

int
ChangeAccessControl(
    ClientPtr client,
    int fEnabled)
{
    if (!AuthorizedClient(client))
	return BadAccess;
    AccessEnabled = fEnabled;
    return Success;
}
