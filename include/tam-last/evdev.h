/* Linux evdev key codes: the numbers the kernel gives each physical key
 * (linux/input-event-codes.h), which Wayland's wl_keyboard.key passes on
 * unchanged. They are a stable kernel ABI. We keep our own copy, so
 * tam-last needs no kernel headers; tests/test_keys.c checks it against
 * the kernel's header when one is installed. */
#ifndef TL_EVDEV_H
#define TL_EVDEV_H

enum tl_evdev {
	TL_KEY_ESC = 1,
	TL_KEY_1 = 2, TL_KEY_2 = 3, TL_KEY_3 = 4, TL_KEY_4 = 5, TL_KEY_5 = 6,
	TL_KEY_6 = 7, TL_KEY_7 = 8, TL_KEY_8 = 9, TL_KEY_9 = 10, TL_KEY_0 = 11,
	TL_KEY_MINUS = 12, TL_KEY_EQUAL = 13, TL_KEY_BACKSPACE = 14,
	TL_KEY_TAB = 15,
	TL_KEY_Q = 16, TL_KEY_W = 17, TL_KEY_E = 18, TL_KEY_R = 19,
	TL_KEY_T = 20, TL_KEY_Y = 21, TL_KEY_U = 22, TL_KEY_I = 23,
	TL_KEY_O = 24, TL_KEY_P = 25,
	TL_KEY_LEFTBRACE = 26, TL_KEY_RIGHTBRACE = 27, TL_KEY_ENTER = 28,
	TL_KEY_LEFTCTRL = 29,
	TL_KEY_A = 30, TL_KEY_S = 31, TL_KEY_D = 32, TL_KEY_F = 33,
	TL_KEY_G = 34, TL_KEY_H = 35, TL_KEY_J = 36, TL_KEY_K = 37,
	TL_KEY_L = 38,
	TL_KEY_SEMICOLON = 39, TL_KEY_APOSTROPHE = 40, TL_KEY_GRAVE = 41,
	TL_KEY_LEFTSHIFT = 42, TL_KEY_BACKSLASH = 43,
	TL_KEY_Z = 44, TL_KEY_X = 45, TL_KEY_C = 46, TL_KEY_V = 47,
	TL_KEY_B = 48, TL_KEY_N = 49, TL_KEY_M = 50,
	TL_KEY_COMMA = 51, TL_KEY_DOT = 52, TL_KEY_SLASH = 53,
	TL_KEY_RIGHTSHIFT = 54, TL_KEY_KPASTERISK = 55, TL_KEY_LEFTALT = 56,
	TL_KEY_SPACE = 57, TL_KEY_CAPSLOCK = 58,
	TL_KEY_F1 = 59, TL_KEY_F2 = 60, TL_KEY_F3 = 61, TL_KEY_F4 = 62,
	TL_KEY_F5 = 63, TL_KEY_F6 = 64, TL_KEY_F7 = 65, TL_KEY_F8 = 66,
	TL_KEY_F9 = 67, TL_KEY_F10 = 68,
	TL_KEY_NUMLOCK = 69, TL_KEY_SCROLLLOCK = 70,
	TL_KEY_KP7 = 71, TL_KEY_KP8 = 72, TL_KEY_KP9 = 73, TL_KEY_KPMINUS = 74,
	TL_KEY_KP4 = 75, TL_KEY_KP5 = 76, TL_KEY_KP6 = 77, TL_KEY_KPPLUS = 78,
	TL_KEY_KP1 = 79, TL_KEY_KP2 = 80, TL_KEY_KP3 = 81, TL_KEY_KP0 = 82,
	TL_KEY_KPDOT = 83,
	TL_KEY_102ND = 86, TL_KEY_F11 = 87, TL_KEY_F12 = 88,
	TL_KEY_KPENTER = 96, TL_KEY_RIGHTCTRL = 97, TL_KEY_KPSLASH = 98,
	TL_KEY_SYSRQ = 99, TL_KEY_RIGHTALT = 100,
	TL_KEY_HOME = 102, TL_KEY_UP = 103, TL_KEY_PAGEUP = 104,
	TL_KEY_LEFT = 105, TL_KEY_RIGHT = 106, TL_KEY_END = 107,
	TL_KEY_DOWN = 108, TL_KEY_PAGEDOWN = 109, TL_KEY_INSERT = 110,
	TL_KEY_DELETE = 111,
	TL_KEY_LEFTMETA = 125, TL_KEY_RIGHTMETA = 126, TL_KEY_COMPOSE = 127,
	TL_KEY_COUNT = 128 /* codes our tables cover */
};

#endif
