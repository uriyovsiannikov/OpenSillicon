#ifndef TINYX_OPENSILLICON_H
#define TINYX_OPENSILLICON_H

#include "kdrive.h"
#include "abi/framebuffer.h"

typedef struct {
	struct os_framebuffer_info info;
	CARD8 *pixels;
} OpenSilliconCard;

Bool opensilliconCardInit(KdCardInfo *card);
Bool opensilliconScreenInit(KdScreenInfo *screen);
Bool opensilliconFinishInitScreen(ScreenPtr screen);
Bool opensilliconCreateResources(ScreenPtr screen);
void opensilliconScreenFini(KdScreenInfo *screen);
void opensilliconCardFini(KdCardInfo *card);

#endif
