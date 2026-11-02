#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#define XTRANS_TRANSPORT_C

#define TRANS_REOPEN
#define TRANS_SERVER
#define XSERV_t
#ifdef OPEN_SILLICON
#define NO_TCP_H 1
#define HAVE_SOCKLEN_T 1
#endif

//#ifdef XSERV_t
#include "os.h"
//#else
#include <stdlib.h>
#define xalloc(_size)		malloc(_size)
#define xcalloc(_num,_size)	calloc(_num,_size)
#define xrealloc(_ptr,_size)	realloc(_ptr,_size)
#define xfree(_ptr)		free(_ptr)
//#endif

#include "xtrans/X11/Xtrans/Xtransint.h"

#ifdef OPEN_SILLICON
/* The kernel's AF_UNIX namespace is opaque and has no socket filesystem nodes. */
#ifndef HAVE_ABSTRACT_SOCKETS
#define HAVE_ABSTRACT_SOCKETS 1
#endif
#endif

#ifdef LOCALCONN
#include "xtrans/X11/Xtrans/Xtranslcl.c"
#endif
#if defined(TCPCONN) || defined(UNIXCONN)
#include "xtrans/X11/Xtrans/Xtranssock.c"
#endif
#include "xtrans/X11/Xtrans/Xtrans.c"
#include "xtrans/X11/Xtrans/Xtransutil.c"
