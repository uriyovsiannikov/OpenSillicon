#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <X11/keysym.h>

#include "opensillicon.h"
#include "kkeymap.h"
#include "abi/input.h"
#include "abi/syscall.h"

#define OPENSILLICON_MAX_SCAN_CODE 0x6fU

static const KdCardFuncs opensilliconFuncs = {
	opensilliconCardInit,
	opensilliconScreenInit,
	0, opensilliconFinishInitScreen, opensilliconCreateResources,
	0, 0, 0, 0, 0,
	opensilliconScreenFini, opensilliconCardFini,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void
opensilliconSetKey(unsigned int scan, KeySym plain, KeySym shifted)
{
	KeySym *key = &kdKeymap[scan * KD_MAX_WIDTH];

	key[0] = plain;
	key[1] = shifted;
}

/* The kernel keeps the E0 prefix separate from the Set 1 scan code. KDrive's
 * event API only accepts one byte; use its navigation key codes for the E0
 * keys, and synthetic codes for Print Screen, modifiers, and keypad keys. */
static unsigned int
opensilliconScanCode(const struct os_keyboard_event *event)
{
	if (!event->extended)
		return event->scancode;
	switch (event->scancode) {
	case 0x1c: return 0x60; /* keypad enter */
	case 0x1d: return 0x61; /* right control */
	case 0x35: return 0x62; /* keypad divide */
	case 0x38: return 0x63; /* right alt */
	case 0x37: return 0x64; /* Print Screen */
	case OS_KEYBOARD_PAUSE_SCANCODE: return 0x65; /* synthetic Pause make */
	case 0x2a: return OPENSILLICON_MAX_SCAN_CODE + 1; /* fake shift */
	case 0x47: return 0x66; /* home */
	case 0x48: return 0x67; /* up */
	case 0x49: return 0x68; /* page up */
	case 0x4b: return 0x69; /* left */
	case 0x4d: return 0x6a; /* right */
	case 0x4f: return 0x6b; /* end */
	case 0x50: return 0x6c; /* down */
	case 0x51: return 0x6d; /* page down */
	case 0x52: return 0x6e; /* insert */
	case 0x53: return 0x6f; /* delete */
	default: return OPENSILLICON_MAX_SCAN_CODE + 1;
	}
}

static void
opensilliconKeyboardLoad(void)
{
	static const KeySym letters[26] = {
		XK_a, XK_b, XK_c, XK_d, XK_e, XK_f, XK_g, XK_h, XK_i,
		XK_j, XK_k, XK_l, XK_m, XK_n, XK_o, XK_p, XK_q, XK_r,
		XK_s, XK_t, XK_u, XK_v, XK_w, XK_x, XK_y, XK_z
	};
	static const uint8_t letter_scans[26] = {
		0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
		0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
		0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
	};
	static const KeySym digits[10] = {
		XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9
	};
	static const KeySym shifted_digits[10] = {
		XK_parenright, XK_exclam, XK_at, XK_numbersign, XK_dollar,
		XK_percent, XK_asciicircum, XK_ampersand, XK_asterisk, XK_parenleft
	};
	static const uint8_t digit_scans[10] = {
		0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a
	};
	static const KeySym punctuation[11][2] = {
		{ XK_minus, XK_underscore }, { XK_equal, XK_plus },
		{ XK_bracketleft, XK_braceleft }, { XK_bracketright, XK_braceright },
		{ XK_semicolon, XK_colon }, { XK_apostrophe, XK_quotedbl },
		{ XK_grave, XK_asciitilde }, { XK_backslash, XK_bar },
		{ XK_comma, XK_less }, { XK_period, XK_greater },
		{ XK_slash, XK_question }
	};
	static const uint8_t punctuation_scans[11] = {
		0x0c, 0x0d, 0x1a, 0x1b, 0x27, 0x28, 0x29, 0x2b, 0x33, 0x34, 0x35
	};
	static const struct {
		uint8_t scan;
		KeySym symbol;
	} keypad[] = {
		{ 0x37, XK_KP_Multiply }, { 0x47, XK_KP_7 },
		{ 0x48, XK_KP_8 }, { 0x49, XK_KP_9 },
		{ 0x4a, XK_KP_Subtract }, { 0x4b, XK_KP_4 },
		{ 0x4c, XK_KP_5 }, { 0x4d, XK_KP_6 },
		{ 0x4e, XK_KP_Add }, { 0x4f, XK_KP_1 },
		{ 0x50, XK_KP_2 }, { 0x51, XK_KP_3 },
		{ 0x52, XK_KP_0 }, { 0x53, XK_KP_Decimal }
	};

	for (unsigned int i = 0; i < KD_MAX_LENGTH * KD_MAX_WIDTH; i++)
		kdKeymap[i] = NoSymbol;
	for (unsigned int i = 0; i < 26; i++)
		opensilliconSetKey(letter_scans[i], letters[i], letters[i] -
		    (XK_a - XK_A));
	for (unsigned int i = 0; i < 10; i++)
		opensilliconSetKey(digit_scans[i], digits[i], shifted_digits[i]);
	for (unsigned int i = 0; i < 11; i++)
		opensilliconSetKey(punctuation_scans[i], punctuation[i][0],
		    punctuation[i][1]);
	for (unsigned int i = 0; i < sizeof(keypad) / sizeof(keypad[0]); i++)
		opensilliconSetKey(keypad[i].scan, keypad[i].symbol,
		    keypad[i].symbol);
	opensilliconSetKey(0x01, XK_Escape, NoSymbol);
	opensilliconSetKey(0x0e, XK_BackSpace, NoSymbol);
	opensilliconSetKey(0x0f, XK_Tab, NoSymbol);
	opensilliconSetKey(0x1c, XK_Return, NoSymbol);
	opensilliconSetKey(0x1d, XK_Control_L, NoSymbol);
	opensilliconSetKey(0x2a, XK_Shift_L, NoSymbol);
	opensilliconSetKey(0x36, XK_Shift_R, NoSymbol);
	opensilliconSetKey(0x38, XK_Alt_L, NoSymbol);
	opensilliconSetKey(0x39, XK_space, NoSymbol);
	opensilliconSetKey(0x3a, XK_Caps_Lock, NoSymbol);
	opensilliconSetKey(0x3b, XK_F1, NoSymbol);
	opensilliconSetKey(0x3c, XK_F2, NoSymbol);
	opensilliconSetKey(0x3d, XK_F3, NoSymbol);
	opensilliconSetKey(0x3e, XK_F4, NoSymbol);
	opensilliconSetKey(0x3f, XK_F5, NoSymbol);
	opensilliconSetKey(0x40, XK_F6, NoSymbol);
	opensilliconSetKey(0x41, XK_F7, NoSymbol);
	opensilliconSetKey(0x42, XK_F8, NoSymbol);
	opensilliconSetKey(0x43, XK_F9, NoSymbol);
	opensilliconSetKey(0x44, XK_F10, NoSymbol);
	opensilliconSetKey(0x45, XK_Num_Lock, NoSymbol);
	opensilliconSetKey(0x46, XK_Scroll_Lock, NoSymbol);
	opensilliconSetKey(0x60, XK_KP_Enter, NoSymbol);
	opensilliconSetKey(0x61, XK_Control_R, NoSymbol);
	opensilliconSetKey(0x62, XK_KP_Divide, NoSymbol);
	opensilliconSetKey(0x63, XK_Alt_R, NoSymbol);
	opensilliconSetKey(0x64, XK_Print, NoSymbol);
	opensilliconSetKey(0x65, XK_Pause, NoSymbol);
	/* These codes match KDrive's keypad-to-navigation remapping table. */
	opensilliconSetKey(0x66, XK_Home, NoSymbol);
	opensilliconSetKey(0x67, XK_Up, NoSymbol);
	opensilliconSetKey(0x68, XK_Page_Up, NoSymbol);
	opensilliconSetKey(0x69, XK_Left, NoSymbol);
	opensilliconSetKey(0x6a, XK_Right, NoSymbol);
	opensilliconSetKey(0x6b, XK_End, NoSymbol);
	opensilliconSetKey(0x6c, XK_Down, NoSymbol);
	opensilliconSetKey(0x6d, XK_Page_Down, NoSymbol);
	opensilliconSetKey(0x6e, XK_Insert, NoSymbol);
	opensilliconSetKey(0x6f, XK_Delete, NoSymbol);
	opensilliconSetKey(0x57, XK_F11, NoSymbol);
	opensilliconSetKey(0x58, XK_F12, NoSymbol);
	kdMinScanCode = 0;
	kdMaxScanCode = 0x6f;
}

static int
opensilliconKeyboardInit(void)
{
	return 1;
}

static void
opensilliconKeyboardNoop(void)
{
}

static void
opensilliconKeyboardLeds(int leds)
{
	(void)leds;
}

static void
opensilliconKeyboardBell(int volume, int pitch, int duration)
{
	(void)volume;
	(void)pitch;
	(void)duration;
}

static const KdKeyboardFuncs opensilliconKeyboardFuncs = {
	opensilliconKeyboardLoad,
	opensilliconKeyboardInit,
	opensilliconKeyboardLeds,
	opensilliconKeyboardBell,
	opensilliconKeyboardNoop,
	0
};

static int
opensilliconMouseInit(void)
{
	return 1;
}

static void
opensilliconMouseFini(void)
{
}

static const KdMouseFuncs opensilliconMouseFuncs = {
	opensilliconMouseInit,
	opensilliconMouseFini
};

static void
opensilliconPollEvents(void)
{
	struct os_keyboard_event event;
	struct os_mouse_event mouse_event;
	unsigned int scan;
	uint64_t result = SYSCALL_KEYBOARD_POLL;
	unsigned char failed;

	__asm__ volatile ("int $0x80; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" ((uintptr_t)&event), "S" (0) : "rcx", "r11", "memory");
	if (!failed && result == 1) {
		scan = opensilliconScanCode(&event);
		if (scan <= OPENSILLICON_MAX_SCAN_CODE) {
			KdEnqueueKeyboardEvent((unsigned char)scan,
			    event.pressed ? 0 : 0x80);
			/* Pause has no PS/2 break code; synthesize its X release. */
			if (event.extended &&
			    event.scancode == OS_KEYBOARD_PAUSE_SCANCODE &&
			    event.pressed)
				KdEnqueueKeyboardEvent((unsigned char)scan, 0x80);
		}
	}
	result = SYSCALL_MOUSE_POLL;
	__asm__ volatile ("int $0x80; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" ((uintptr_t)&mouse_event), "S" (0) : "rcx", "r11", "memory");
	if (!failed && result == 1 && kdMouseInfo != NULL) {
		unsigned long buttons = 0;
		if (mouse_event.buttons & 0x01U)
			buttons |= KD_BUTTON_1;
		if (mouse_event.buttons & 0x04U)
			buttons |= KD_BUTTON_2;
		if (mouse_event.buttons & 0x02U)
			buttons |= KD_BUTTON_3;
		KdEnqueueMouseEvent(kdMouseInfo, KD_MOUSE_DELTA | buttons,
		    mouse_event.dx, mouse_event.dy);
	}
}

static int
opensilliconOsInit(void)
{
	return 0;
}

static const KdOsFuncs opensilliconOsFuncs = {
	opensilliconOsInit,
	0,
	0,
	0,
	opensilliconKeyboardNoop,
	opensilliconPollEvents
};

void
OsVendorInit(void)
{
	KdOsInit(&opensilliconOsFuncs);
}

void
InitCard(char *name)
{
	(void)name;
	KdCardInfoAdd(&opensilliconFuncs, NULL);
}

void
InitOutput(ScreenInfo *screenInfo, int argc, char **argv)
{
	KdInitOutput(screenInfo, argc, argv);
}

void
InitInput(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	KdInitInput(&opensilliconMouseFuncs, &opensilliconKeyboardFuncs);
}

void
ddxUseMsg(void)
{
	KdUseMsg();
	ErrorF("\nXopensillicon uses the boot framebuffer.\n");
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
	return KdProcessArgument(argc, argv, i);
}
