/*

Copyright 1993, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

 * Copyright 1993, 1994 NCR Corporation - Dayton, Ohio, USA
 *
 * All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name NCR not be used in advertising
 * or publicity pertaining to distribution of the software without specific,
 * written prior permission.  NCR makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * NCR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NCR BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>

/*
 * The transport table contains a definition for every transport (protocol)
 * family. All operations that can be made on the transport go through this
 * table.
 *
 * Each transport is assigned a unique transport id.
 *
 * New transports can be added by adding an entry in this table.
 * For compatibility, the transport ids should never be renumbered.
 * Always add to the end of the list.
 */

#define TRANS_SOCKET_LOCAL_INDEX	5

static
Xtransport_table Xtransports[] = {
#if defined(UNIXCONN)
    { &TRANS(SocketLocalFuncs),	TRANS_SOCKET_LOCAL_INDEX },
#endif /* UNIXCONN */
};

#define NUMTRANS	(sizeof(Xtransports)/sizeof(Xtransport_table))





/*
 * These are a few utility function used by the public interface functions.
 */

void
TRANS(FreeConnInfo) (XtransConnInfo ciptr)

{
    prmsg (3,"FreeConnInfo(%p)\n", (void *) ciptr);

    if (ciptr->addr)
	free (ciptr->addr);

    if (ciptr->peeraddr)
	free (ciptr->peeraddr);

    free (ciptr);
}


#define PROTOBUFSIZE	20

static Xtransport *
TRANS(SelectTransport) (const char *protocol)

{
#ifndef HAVE_STRCASECMP
    char 	protobuf[PROTOBUFSIZE];
#endif

    prmsg (3,"SelectTransport(%s)\n", protocol);

#ifndef HAVE_STRCASECMP
    /*
     * Force Protocol to be lowercase as a way of doing
     * a case insensitive match.
     */

    strncpy (protobuf, protocol, PROTOBUFSIZE - 1);
    protobuf[PROTOBUFSIZE-1] = '\0';

    for (unsigned int i = 0; i < PROTOBUFSIZE && protobuf[i] != '\0'; i++)
	if (isupper ((unsigned char)protobuf[i]))
	    protobuf[i] = tolower ((unsigned char)protobuf[i]);
#endif

    /* Look at all of the configured protocols */

    for (unsigned int i = 0; i < NUMTRANS; i++)
    {
#ifndef HAVE_STRCASECMP
	if (!strcmp (protobuf, Xtransports[i].transport->TransName))
#else
	if (!strcasecmp (protocol, Xtransports[i].transport->TransName))
#endif
	    return Xtransports[i].transport;
    }

    return NULL;
}

static int
TRANS(ParseAddress) (const char *address,
                     char **protocol, char **host, char **port)

{
    /*
     * For the font library, the address is a string formatted
     * as "protocol/host:port[/catalogue]".  Note that the catologue
     * is optional.  At this time, the catologue info is ignored, but
     * we have to parse it anyways.
     *
     * Other than fontlib, the address is a string formatted
     * as "protocol/host:port".
     *
     * If the protocol part is missing, a hostless display is local.
     * If the protocol part and host part are missing, then assume local.
     * TinyX accepts local UNIX sockets only.
     */

    char	*mybuf, *tmpptr = NULL;
    const char	*_protocol = NULL;
    const char	*_host, *_port;
    char	*_host_buf;

    prmsg (3,"ParseAddress(%s)\n", address);

    /* First, check for AF_UNIX socket paths */
    if (address[0] == '/') {
        _protocol = "local";
        _host = "";
        _port = address;
    } else
    if (!strncmp(address, "unix:", 5)) {
        _protocol = "local";
        _host = "";
        _port = address + 5;
    }
    if (_protocol)
        goto done_parsing;

    /* Copy the string so it can be changed */

    tmpptr = mybuf = strdup (address);

    /* Parse the string to get each component */

    /* Get the protocol part */

    _protocol = mybuf;


    if ((mybuf == NULL) ||
        ( ((mybuf = strchr (mybuf, '/')) == NULL) &&
          ((mybuf = strrchr (tmpptr, ':')) == NULL) ) )
    {
	/* address is in a bad format */
	*protocol = NULL;
	*host = NULL;
	*port = NULL;
	free (tmpptr);
	return 0;
    }

    if (*mybuf == ':')
    {
	if (mybuf != tmpptr) {
	    *protocol = NULL;
	    *host = NULL;
	    *port = NULL;
	    free (tmpptr);
	    return 0;
	}
	_protocol = "local";
    }
    else
    {
	/* *mybuf == '/' */

	*mybuf ++= '\0'; /* put a null at the end of the protocol */

	if (strlen(_protocol) == 0)
	{
	    if (*mybuf != ':') {
		*protocol = NULL;
		*host = NULL;
		*port = NULL;
		free (tmpptr);
		return 0;
	    }
	    _protocol = "local";
	}
    }

    /* Get the host part */

    _host = _host_buf = mybuf;

    if ((mybuf = strrchr (mybuf,':')) == NULL)
    {
	*protocol = NULL;
	*host = NULL;
	*port = NULL;
	free (tmpptr);
	return 0;
    }

    *mybuf ++= '\0';

    /* Get the port */

    _port = mybuf;

done_parsing:
    /*
     * Now that we have all of the components, allocate new
     * string space for them.
     */

    if ((*protocol = strdup (_protocol)) == NULL)
    {
	/* Malloc failed */
	*port = NULL;
	*host = NULL;
	*protocol = NULL;
	free (tmpptr);
	return 0;
    }

    if ((*host = strdup (_host)) == NULL)
    {
	/* Malloc failed */
	*port = NULL;
	*host = NULL;
	free (*protocol);
	*protocol = NULL;
	free (tmpptr);
	return 0;
    }

    if ((*port = strdup (_port)) == NULL)
    {
	/* Malloc failed */
	*port = NULL;
	free (*host);
	*host = NULL;
	free (*protocol);
	*protocol = NULL;
	free (tmpptr);
	return 0;
    }

    free (tmpptr);

    return 1;
}


/*
 * TRANS(Open) does all of the real work opening a connection. The only
 * funny part about this is the type parameter which is used to decide which
 * type of open to perform.
 */

static XtransConnInfo
TRANS(Open) (int type, const char *address)

{
    char 		*protocol = NULL, *host = NULL, *port = NULL;
    XtransConnInfo	ciptr = NULL;
    Xtransport		*thistrans;

    prmsg (2,"Open(%d,%s)\n", type, address);


    /* Parse the Address */

    if (TRANS(ParseAddress) (address, &protocol, &host, &port) == 0)
    {
	prmsg (1,"Open: Unable to Parse address %s\n", address);
	return NULL;
    }

    /* Determine the transport type */

    if ((thistrans = TRANS(SelectTransport) (protocol)) == NULL)
    {
	prmsg (1,"Open: Unable to find transport for %s\n",
	       protocol);

	free (protocol);
	free (host);
	free (port);
	return NULL;
    }

    /* Open the transport */

    switch (type)
    {
    case XTRANS_OPEN_COTS_SERVER:
#ifdef TRANS_SERVER
	ciptr = thistrans->OpenCOTSServer(thistrans, protocol, host, port);
#endif /* TRANS_SERVER */
	break;
    default:
	prmsg (1,"Open: Unknown Open type %d\n", type);
    }

    if (ciptr == NULL)
    {
	prmsg (1,"Open: transport open failed for %s/%s:%s\n",
	       protocol, host, port);
	free (protocol);
	free (host);
	free (port);
	return NULL;
    }

    ciptr->transptr = thistrans;
    free (protocol);
    free (host);
    free (port);

    return ciptr;
}





/*
 * These are the public interfaces to this Transport interface.
 * These are the only functions that should have knowledge of the transport
 * table.
 */



#ifdef TRANS_SERVER

XtransConnInfo
TRANS(OpenCOTSServer) (const char *address)

{
    prmsg (2,"OpenCOTSServer(%s)\n", address);
    return TRANS(Open) (XTRANS_OPEN_COTS_SERVER, address);
}

#endif /* TRANS_SERVER */




int
TRANS(SetOption) (XtransConnInfo ciptr, int option, int arg)

{
    int	fd = ciptr->fd;
    int	ret = 0;

    prmsg (2,"SetOption(%d,%d,%d)\n", fd, option, arg);

    /*
     * For now, all transport type use the same stuff for setting options.
     * As long as this is true, we can put the common code here. Once a more
     * complicated transport such as shared memory or an OSI implementation
     * that uses the session and application libraries is implemented, this
     * code may have to move to a transport dependent function.
     *
     */

    switch (option)
    {
    case TRANS_NONBLOCKING:
	switch (arg)
	{
	case 0:
	    /* Set to blocking mode */
	    break;
	case 1: /* Set to non-blocking mode */

#if defined(O_NONBLOCK)
	    ret = fcntl (fd, F_GETFL, 0);
	    if (ret != -1)
		ret = fcntl (fd, F_SETFL, ret | O_NONBLOCK);
#else
#ifdef FIOSNBIO
	{
	    int arg;
	    arg = 1;
	    ret = ioctl (fd, FIOSNBIO, &arg);
	}
#else
	    ret = fcntl (fd, F_GETFL, 0);
#ifdef FNDELAY
	    ret = fcntl (fd, F_SETFL, ret | FNDELAY);
#else
	    ret = fcntl (fd, F_SETFL, ret | O_NDELAY);
#endif
#endif /* FIOSNBIO */
#endif /* O_NONBLOCK */
	    break;
	default:
	    /* Unknown option */
	    break;
	}
	break;
    }

    return ret;
}

#ifdef TRANS_SERVER

int
TRANS(CreateListener) (XtransConnInfo ciptr, const char *port, unsigned int flags)

{
    return ciptr->transptr->CreateListener (ciptr, port, flags);
}


int
TRANS(NoListen) (const char *protocol)
{
    Xtransport *trans = TRANS(SelectTransport)(protocol);

    if (!trans) {
        prmsg (1,"TransNoListen: unable to find transport: %s\n", protocol);
        return -1;
    }
    trans->flags |= TRANS_NOLISTEN;
    return 0;
}

int
TRANS(ResetListener) (XtransConnInfo ciptr)

{
    if (ciptr->transptr->ResetListener)
	return ciptr->transptr->ResetListener (ciptr);
    else
	return TRANS_RESET_NOOP;
}


XtransConnInfo
TRANS(Accept) (XtransConnInfo ciptr, int *status)

{
    XtransConnInfo	newciptr;

    prmsg (2,"Accept(%d)\n", ciptr->fd);

    newciptr = ciptr->transptr->Accept (ciptr, status);

    if (newciptr)
	newciptr->transptr = ciptr->transptr;

    return newciptr;
}

#endif /* TRANS_SERVER */




int
TRANS(BytesReadable) (XtransConnInfo ciptr, BytesReadable_t *pend)

{
    return ciptr->transptr->BytesReadable (ciptr, pend);
}

int
TRANS(Read) (XtransConnInfo ciptr, char *buf, int size)

{
    return ciptr->transptr->Read (ciptr, buf, size);
}

int
TRANS(Write) (XtransConnInfo ciptr, const char *buf, int size)

{
    return ciptr->transptr->Write (ciptr, buf, size);
}

int
TRANS(Readv) (XtransConnInfo ciptr, struct iovec *buf, int size)

{
    return ciptr->transptr->Readv (ciptr, buf, size);
}

int
TRANS(Writev) (XtransConnInfo ciptr, struct iovec *buf, int size)

{
    return ciptr->transptr->Writev (ciptr, buf, size);
}


int
TRANS(Disconnect) (XtransConnInfo ciptr)

{
    return ciptr->transptr->Disconnect (ciptr);
}

int
TRANS(Close) (XtransConnInfo ciptr)

{
    int ret;

    prmsg (2,"Close(%d)\n", ciptr->fd);

    ret = ciptr->transptr->Close (ciptr);

    TRANS(FreeConnInfo) (ciptr);

    return ret;
}

int
TRANS(IsLocal) (XtransConnInfo ciptr)

{
    return (ciptr->family == AF_UNIX);
}

int
TRANS(GetPeerAddr) (XtransConnInfo ciptr, int *familyp, int *addrlenp,
		    Xtransaddr **addrp)

{
    prmsg (2,"GetPeerAddr(%d)\n", ciptr->fd);

    *familyp = ciptr->family;
    *addrlenp = ciptr->peeraddrlen;

    if ((*addrp = malloc (ciptr->peeraddrlen)) == NULL)
    {
	prmsg (1,"GetPeerAddr: malloc failed\n");
	return -1;
    }
    memcpy(*addrp, ciptr->peeraddr, ciptr->peeraddrlen);

    return 0;
}


int
TRANS(GetConnectionNumber) (XtransConnInfo ciptr)

{
    return ciptr->fd;
}


/*
 * These functions are really utility functions, but they require knowledge
 * of the internal data structures, so they have to be part of the Transport
 * Independent API.
 */

#ifdef TRANS_SERVER

static int
complete_network_count (void)

{
    int count = 0;
    int found_local = 0;

    /*
     * For TinyX, the local UNIX transport is the complete connection set.
     */

    for (unsigned int i = 0; i < NUMTRANS; i++)
    {
	if (Xtransports[i].transport->flags & TRANS_NOLISTEN)
	    continue;

	if (Xtransports[i].transport->flags & TRANS_LOCAL)
	    found_local = 1;
	else
	    count++;
    }

    return (count + found_local);
}



int
TRANS(MakeAllCOTSServerListeners) (const char *port, int *partial,
                                   int *count_ret, XtransConnInfo **ciptrs_ret)

{
    char		buffer[256]; /* ??? What size ?? */
    XtransConnInfo	ciptr, temp_ciptrs[NUMTRANS] = { NULL };
    int			status, j;

    prmsg (2,"MakeAllCOTSServerListeners(%s,%p)\n",
	   port ? port : "NULL", (void *) ciptrs_ret);

    *count_ret = 0;

    for (unsigned int i = 0; i < NUMTRANS; i++)
    {
	Xtransport *trans = Xtransports[i].transport;
	unsigned int flags = 0;

	if (trans->flags & TRANS_NOLISTEN)
	    continue;

	snprintf(buffer, sizeof(buffer), "%s/:%s",
		 trans->TransName, port ? port : "");

	prmsg (5,"MakeAllCOTSServerListeners: opening %s\n",
	       buffer);

	if ((ciptr = TRANS(OpenCOTSServer(buffer))) == NULL)
	{
	    prmsg (1,
	  "MakeAllCOTSServerListeners: failed to open listener for %s\n",
		  trans->TransName);
	    continue;
	}
	if ((status = TRANS(CreateListener (ciptr, port, flags))) < 0)
	{
            if (*partial != 0)
		continue;

	    if (status == TRANS_ADDR_IN_USE)
	    {
		/*
		 * We failed to bind to the specified address because the
		 * address is in use.  It must be that a server is already
		 * running at this address, and this function should fail.
		 */

		prmsg (1,
		"MakeAllCOTSServerListeners: server already running\n");

		for (j = 0; j < *count_ret; j++)
		    if (temp_ciptrs[j] != NULL)
			TRANS(Close) (temp_ciptrs[j]);

		*count_ret = 0;
		*ciptrs_ret = NULL;
		*partial = 0;
		return -1;
	    }
	    else
	    {
		prmsg (1,
	"MakeAllCOTSServerListeners: failed to create listener for %s\n",
		  trans->TransName);

		continue;
	    }
	}

	prmsg (5,
	      "MakeAllCOTSServerListeners: opened listener for %s, %d\n",
	      trans->TransName, ciptr->fd);

	temp_ciptrs[*count_ret] = ciptr;
	(*count_ret)++;
    }

    *partial = (*count_ret < complete_network_count());

    prmsg (5,
     "MakeAllCOTSServerListeners: partial=%d, actual=%d, complete=%d \n",
	*partial, *count_ret, complete_network_count());

    if (*count_ret > 0)
    {
	if ((*ciptrs_ret = malloc (
	    *count_ret * sizeof (XtransConnInfo))) == NULL)
	{
	    return -1;
	}

	for (int i = 0; i < *count_ret; i++)
	{
	    (*ciptrs_ret)[i] = temp_ciptrs[i];
	}
    }
    else
	*ciptrs_ret = NULL;

    return 0;
}

#endif /* TRANS_SERVER */



/*
 * These routines are not part of the X Transport Interface, but they
 * may be used by it.
 */




#if defined(_POSIX_SOURCE) || defined(SVR4) || defined(__SVR4)
#ifndef NEED_UTSNAME
#define NEED_UTSNAME
#endif
#include <sys/utsname.h>
#endif

/*
 * TRANS(GetHostname) - similar to gethostname but allows special processing.
 */

int TRANS(GetHostname) (char *buf, int maxlen)

{
    int len;

#ifdef NEED_UTSNAME
    struct utsname name;

    uname (&name);
    len = strlen (name.nodename);
    if (len >= maxlen) len = maxlen - 1;
    memcpy (buf, name.nodename, len);
    buf[len] = '\0';
#else
    buf[0] = '\0';
    (void) gethostname (buf, maxlen);
    buf [maxlen - 1] = '\0';
    len = strlen(buf);
#endif /* NEED_UTSNAME */
    return len;
}
