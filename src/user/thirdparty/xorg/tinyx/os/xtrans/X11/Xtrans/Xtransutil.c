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
 * NCRS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NCR BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * These are some utility functions created for convenience or to provide
 * an interface that is similar to an existing interface. These are built
 * only using the Transport Independent API, and have no knowledge of
 * the internal implementation.
 */

#ifdef X11_t

/*
 * These values come from X.h and Xauth.h, and MUST match them. Some
 * of these values are also defined by the ChangeHost protocol message.
 */

#define FamilyLocal		256

/*
 * TRANS(ConvertAddress) converts a sockaddr based address to an
 * X authorization based address. Some of this is defined as part of
 * the ChangeHost protocol. The rest is just done in a consistent manner.
 */

int
TRANS(ConvertAddress)(int *familyp, int *addrlenp, Xtransaddr **addrp)

{

    prmsg(2,"ConvertAddress(%d,%d,%p)\n",*familyp,*addrlenp,*addrp);

    switch( *familyp )
    {


#ifdef UNIXCONN
    case AF_UNIX:
    {
	*familyp=FamilyLocal;
	break;
    }
#endif /* UNIXCONN */


    default:
	prmsg(1,"ConvertAddress: Unknown family type %d\n",
	      *familyp);
	return -1;
    }


    if (*familyp == FamilyLocal)
    {
	/*
	 * In the case of a local connection, we need to get the
	 * host name for authentication.
	 */

	char hostnamebuf[256];
	int len = TRANS(GetHostname) (hostnamebuf, sizeof hostnamebuf);

	if (len > 0) {
	    if (*addrp && *addrlenp < (len + 1))
	    {
		free (*addrp);
		*addrp = NULL;
	    }
	    if (!*addrp)
		*addrp = malloc (len + 1);
	    if (*addrp) {
		strcpy ((char *) *addrp, hostnamebuf);
		*addrlenp = len;
	    } else {
		*addrlenp = 0;
	    }
	}
	else
	{
	    if (*addrp)
		free (*addrp);
	    *addrp = NULL;
	    *addrlenp = 0;
	}
    }

    return 0;
}

#endif /* X11_t */
