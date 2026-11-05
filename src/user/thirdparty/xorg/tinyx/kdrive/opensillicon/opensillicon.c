#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "opensillicon.h"
#include "abi/syscall.h"

static long
os_syscall2(uint64_t number, uint64_t first, uint64_t second)
{
	uint64_t result = number;
	unsigned char failed;

	__asm__ volatile ("int $0x80; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (first), "S" (second) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

static void
opensilliconFramebufferPresent(ScreenPtr pScreen, shadowBufPtr pBuf)
{
	KdScreenPriv(pScreen);
	KdScreenInfo *screen = pScreenPriv->screen;
	OpenSilliconCard *card = screen->card->driver;
	RegionPtr damage = shadowDamage(pBuf);
	BoxPtr boxes;
	int count;
	struct os_framebuffer_rect rect;

	/* With no rotation, the screen pixmap already uses card->pixels.
	 * Avoid copying each damaged pixel back to the same address. */
	if (pBuf->pPixmap->devPrivate.ptr != card->pixels ||
	    pBuf->pPixmap->devKind != (int)card->info.pitch)
		shadowUpdatePacked(pScreen, pBuf);
	/* Keep disjoint damage disjoint all the way to the device.  Its bounding
	 * box can cover most of the screen for two small, distant updates. */
	count = REGION_NUM_RECTS(damage);
	boxes = REGION_RECTS(damage);
	for (int i = 0; i < count; i++) {
		BoxPtr box = &boxes[i];

		if (box->x1 < 0 || box->y1 < 0 || box->x2 <= box->x1 ||
		    box->y2 <= box->y1)
			continue;
		rect = (struct os_framebuffer_rect){
			.source = (uintptr_t)card->pixels,
			.x = box->x1,
			.y = box->y1,
			.width = box->x2 - box->x1,
			.height = box->y2 - box->y1
		};
		if (os_syscall2(SYSCALL_FRAMEBUFFER_PRESENT_RECT,
		    (uintptr_t)&rect, 0) < 0) {
			ErrorF("OpenSillicon: framebuffer present syscall failed\n");
			return;
		}
	}
}

static void *
opensilliconWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
	CARD32 *size, void *closure)
{
	KdScreenPriv(pScreen);
	OpenSilliconCard *card = pScreenPriv->card->driver;
	uint64_t row_offset;

	(void)closure;
	(void)mode;
	if (row >= card->info.height || offset >= card->info.pitch)
		return NULL;
	row_offset = (uint64_t)row * card->info.pitch + offset;
	if (row_offset >= card->info.size)
		return NULL;
	*size = (CARD32)(card->info.pitch - offset);
	return card->pixels + row_offset;
}

Bool
opensilliconCardInit(KdCardInfo *card)
{
	OpenSilliconCard *priv = calloc(1, sizeof(*priv));
	struct os_framebuffer_info *info;

	if (priv == NULL)
		return FALSE;
	info = &priv->info;
	if (os_syscall2(SYSCALL_FRAMEBUFFER_INFO, (uintptr_t)info, 0) < 0 ||
	    info->width == 0 || info->height == 0 ||
	    info->width > INT_MAX || info->height > INT_MAX ||
	    info->bits_per_pixel != 32 || info->pitch < info->width * 4 ||
	    info->pitch > INT_MAX ||
	    info->height > SIZE_MAX / info->pitch ||
	    info->size != info->height * info->pitch ||
	    info->red_mask_size == 0 || info->green_mask_size == 0 ||
	    info->blue_mask_size == 0 || info->red_mask_size > 8 ||
	    info->green_mask_size > 8 || info->blue_mask_size > 8 ||
	    info->red_mask_shift + info->red_mask_size > 32 ||
	    info->green_mask_shift + info->green_mask_size > 32 ||
	    info->blue_mask_shift + info->blue_mask_size > 32) {
		free(priv);
		return FALSE;
	}
	card->driver = priv;
	return TRUE;
}

Bool
opensilliconScreenInit(KdScreenInfo *screen)
{
	OpenSilliconCard *card = screen->card->driver;
	struct os_framebuffer_info *info = &card->info;
	uint32_t red_mask, green_mask, blue_mask;

	if (screen->randr != 0 && screen->randr != RR_Rotate_0)
		return FALSE;
	screen->randr = RR_Rotate_0;
	if (screen->width != 0 && (uint64_t)screen->width != info->width)
		return FALSE;
	if (screen->height != 0 && (uint64_t)screen->height != info->height)
		return FALSE;

	red_mask = ((1U << info->red_mask_size) - 1U) << info->red_mask_shift;
	green_mask = ((1U << info->green_mask_size) - 1U) << info->green_mask_shift;
	blue_mask = ((1U << info->blue_mask_size) - 1U) << info->blue_mask_shift;
	screen->width = (int)info->width;
	screen->height = (int)info->height;
	screen->fb.depth = 24;
	screen->fb.bitsPerPixel = 32;
	screen->fb.visuals = 1 << TrueColor;
	screen->fb.redMask = red_mask;
	screen->fb.greenMask = green_mask;
	screen->fb.blueMask = blue_mask;
	screen->fb.byteStride = (int)info->pitch;
	screen->fb.pixelStride = (int)(info->pitch / 4);
	card->pixels = calloc(1, (size_t)info->size);
	if (card->pixels == NULL)
		return FALSE;
	screen->fb.frameBuffer = card->pixels;
	screen->fb.shadow = TRUE;
	screen->memory_base = card->pixels;
	screen->memory_size = (unsigned long)info->size;
	screen->off_screen_base = screen->memory_size;
	return TRUE;
}

Bool
opensilliconFinishInitScreen(ScreenPtr pScreen)
{
	return shadowSetup(pScreen);
}

Bool
opensilliconCreateResources(ScreenPtr pScreen)
{
	return KdShadowSet(pScreen, RR_Rotate_0,
	    opensilliconFramebufferPresent, opensilliconWindow);
}

void
opensilliconScreenFini(KdScreenInfo *screen)
{
	KdShadowFbFree(screen);
	screen->fb.frameBuffer = NULL;
	screen->fb.shadow = FALSE;
	((OpenSilliconCard *)screen->card->driver)->pixels = NULL;
}

void
opensilliconCardFini(KdCardInfo *card)
{
	free(card->driver);
	card->driver = NULL;
}
