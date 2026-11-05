
#ifndef _XSERV_GLOBAL_H_
#define _XSERV_GLOBAL_H_

#include "window.h"	/* for WindowPtr */

/* Global X server variables that are visible to mi, dix, os, and ddx */

extern CARD32 defaultScreenSaverTime;
extern CARD32 defaultScreenSaverInterval;
extern CARD32 ScreenSaverTime;
extern CARD32 ScreenSaverInterval;


extern char *defaultFontPath;
extern int monitorResolution;
extern int defaultColorVisualClass;

extern Bool Must_have_memory;
extern WindowPtr WindowTable[MAXSCREENS];
extern int GrabInProgress;

extern DDXPointRec dixScreenOrigins[MAXSCREENS];


extern Bool noBigReqExtension;

extern Bool noDamageExtension;







extern Bool noRRExtension;

extern Bool noRenderExtension;

extern Bool noShapeExtension;

extern Bool noSyncExtension;






extern Bool noXFixesExtension;


#endif /* !_XSERV_GLOBAL_H_ */
