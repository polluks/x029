/*
 * Copyright (c) 1993-2025 Paul Mattes.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes, Jeff Sparkes, GTRC nor the names of
 *       their contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * x029 -- A Keypunch Simluator
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/time.h>
#include <inttypes.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Core.h>
#include <X11/Shell.h>
#include <X11/Xatom.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/Porthole.h>
#include <X11/xpm.h>

#include "jones.h"		/* file format definitions */
#include "charset.h"		/* character set module */
#include "cardimg.h"		/* card image module */
#include "x029.h"		/* global definitions for x029 */
#include "paste.h"		/* paste module */
#include "save.h"		/* save module */
#include "cardimg_menu.h"	/* card image menu */
#include "charset_menu.h"	/* character set menu */
#include "eventq.h"		/* event queueing module */

#include "hole.xpm"		/* hole image */
#include "flipper_off.xpm"	/* power switch, off */
#include "flipper_on.xpm"	/* power switch, on */
#include "off60.xpm"		/* toggle switch, off */
#include "on60.xpm"		/* toggle switch, on */
#include "feed.xpm"		/* FEED key */
#include "feed_pressed.xpm"	/* FEED key */
#include "rel.xpm"		/* REL key */
#include "rel_pressed.xpm"	/* REL key */
#include "save.xpm"		/* SAVE button */
#include "save_pressed.xpm"	/* SAVE button */
#include "drop.xpm"		/* DROP button */
#include "drop_pressed.xpm"	/* DROP button */
#include "ci2.xpm"		/* column indicator */
#include "arrow.xpm"		/* arrow */
#include "x029.bm"		/* icon */

#define VERSION "x029 2.0"

enum {
    T_AUTO_SKIP_DUP,
    T_UNUSED_1,
    T_PROG_SEL,
    T_AUTO_FEED,
    T_PRINT,
    T_LZ_PRINT,
    T_UNUSED_2,
    T_CLEAR
} toggle_ix;

char *top_label[] = { "ON", NULL, "ONE", "ON", "ON", "ON", NULL, "ON" };
char *bottom_label1[] = { "AUTO", NULL, "TWO", "AUTO", "PRINT", "LZ", NULL, "CLEAR" };
char *bottom_label2[] = { "SKIP", NULL, "PROG", "FEED", NULL, "PRINT", NULL, NULL };
char *bottom_label3[] = { "DUP", NULL, "SEL", NULL, NULL, NULL, NULL, NULL };

#define VERY_SLOW 500
#define SLOW	75
#define FAST	25
#define VERY_FAST 15

#define SLAM_COL	40
#define SLAM_TARGET_COL	52

#define CELL_X_NUM	693
#define CELL_X_DENOM	80
#define CELL_WIDTH	(CELL_X_NUM / CELL_X_DENOM)
#define CELL_X(col)	(((col) * CELL_X_NUM) / CELL_X_DENOM)
#define COL_FROM_X(x)	(((x) * CELL_X_DENOM) / CELL_X_NUM)

#define CELL_Y_NUM	296
#define CELL_Y_DENOM	12
#define CELL_HEIGHT	(CELL_Y_NUM / CELL_Y_DENOM)
#define CELL_Y(row)	(((row) * CELL_Y_NUM) / CELL_Y_DENOM)
#define ROW_FROM_Y(y)	(((y) * CELL_Y_DENOM) / CELL_Y_NUM)

/*
 * Vertical stacking:
 *
 * POSW_HEIGHT		POSW_TFRAME
 * 			POSW_INNER_HEIGHT
 * 			POSW_FRAME
 * MECH_HEIGHT		MECH_TFRAME
 * 			CHANNEL_HEIGHT		CHANNEL_TFRAME
 * 						CARD_HEIGHT
 * 						CHANNEL_BFRAME
 * 			MECH_BFRAME
 * KEYBOX_HEIGHT	KEYBOX_BORDER
 * 			KEYBOX_INNER_HEIGHT	SWITCHES_HEIGHT	SWITCHES_TFRAME
 * 								SWITCH_PANEL_HEIGHT
 * 								SWITCHES_BFRAME
 * 			KEYBOX_BORDER
 * 						KEYBOARD_HEIGHT	KEYBOARD_TFRAME
 * 								KEY_HEIGHT
 * 								KEYBOARD_BFRAME
 * DESK_HEIGHT		DESK_FRAME
 * 			DESK_THICKNESS
 * 			DESK_FRAME
 * BASE_HEIGHT		CARD_AIR
 * 			CARDIMG_MENU_HEIGHT
 * 			CARD_AIR
 */
#define POSW_TFRAME	8
#define POSW_INNER_HEIGHT 29
#define POSW_FRAME	4
#define  POSW_HEIGHT	(POSW_TFRAME + POSW_INNER_HEIGHT + POSW_FRAME)

#define MECH_TFRAME	10
#define CHANNEL_Y	(POSW_HEIGHT + MECH_TFRAME)
#define CHANNEL_X	0

#define CHANNEL_TFRAME	15
#define CARD_HEIGHT	331
#define CHANNEL_BFRAME	10
#define  CHANNEL_HEIGHT	(CHANNEL_TFRAME + CARD_HEIGHT + CHANNEL_BFRAME)

#define MECH_BFRAME	20

#define  MECH_HEIGHT	(MECH_TFRAME + CHANNEL_HEIGHT + MECH_BFRAME)

#define KEYBOX_Y	(POSW_HEIGHT + MECH_HEIGHT)

#define	SWITCHES_TFRAME	20
#define	SWITCH_PANEL_HEIGHT 100
#define SWITCHES_BFRAME	20
#define  SWITCHES_HEIGHT (SWITCHES_TFRAME + SWITCH_PANEL_HEIGHT + SWITCHES_BFRAME)

#define KEYBOARD_TFRAME	10
#define KEYBOARD_BFRAME	10
#define KEYBOARD_LRFRAME 10
#define  KEYBOARD_HEIGHT (KEYBOARD_TFRAME + KEY_HEIGHT + KEYBOARD_BFRAME)

#define  KEYBOX_INNER_HEIGHT	(SWITCHES_HEIGHT + KEYBOX_BORDER + KEYBOARD_HEIGHT)

#define KEYBOX_BORDER	CARD_AIR
#define KEYBOX_WIDTH	w
#define  KEYBOX_HEIGHT	(KEYBOX_BORDER + KEYBOX_INNER_HEIGHT)
#define KEYBOARD_WIDTH	(w - 2*KEYBOX_BORDER)
#define KEYBOARD_Y	(KEYBOX_BORDER + SWITCHES_HEIGHT + KEYBOX_BORDER)
#define KEYBOARD_X	KEYBOX_BORDER

#define DESK_Y		(KEYBOX_Y + KEYBOX_HEIGHT)

#define DESK_FRAME	1
#define DESK_THICKNESS	20
#define  DESK_HEIGHT	(2*DESK_FRAME + DESK_THICKNESS)

#define BASE_Y		(DESK_Y + DESK_HEIGHT)

#define BASE_HEIGHT	(2*CARD_AIR + CARDIMG_MENU_HEIGHT)

#define TOTAL_HEIGHT	(POSW_HEIGHT + MECH_HEIGHT + KEYBOX_HEIGHT + DESK_HEIGHT + BASE_HEIGHT)

/* ... */

#define SWITCH_AIR	40
#define SWITCH_HEIGHT	60
#define SWITCH_WIDTH	42
#define SWITCH_SKIP	(2*SWITCH_AIR + SWITCH_HEIGHT)

#define TOP_PAD		15
#define TEXT_PAD	8
#define HOLE_PAD	11
#define LEFT_PAD	31
#define RIGHT_PAD	15
#define BOTTOM_PAD	15
#define CARD_AIR	5

#define	BUTTON_GAP	5
#define BUTTON_BW	2
#define BUTTON_WIDTH	45
#define	BUTTON_HEIGHT	20

#define POSW_INNER_WIDTH	(KEY_WIDTH * 3)
#define POSW_WIDTH	(POSW_FRAME + POSW_INNER_WIDTH + POSW_FRAME)
#define ARROW_WIDTH	19

#define STACKER_WIDTH	43

#define KEY_WIDTH	40
#define KEY_HEIGHT	40

#define POWER_GAP	10
#define POWER_WIDTH	30
#define POWER_HEIGHT	40

Widget			toplevel;
Display			*display;
int			default_screen;
XtAppContext		appcontext;

static char		*programname;
static int		root_window;
static int		depth;
static XFontStruct	*ifontinfo;
Atom			a_delete_me;
static int		line_number = 100;
static int		card_count = 0;
static Pixmap		hole_pixmap;

static Pixmap		flipper_off, flipper_on;
static Widget		power_widget;
static Widget		stacker;

static charset_t	ccharset = NULL;
static cardimg_t	ccardimg = NULL;
static cardimg_t	ncardimg = NULL;

static Position		ps_offset;

int			ap_fd = -1;
bool			did_auto_rel = false;

/* Mode: interactive versus auto-play. */
typedef enum {
    M_INTERACTIVE,		/* interactive */
    M_BATCH,			/* read from a fixed file */
    M_REMOTECTL			/* read from stdin incrementally */
} imode_t;
static imode_t mode = M_INTERACTIVE;

/* 029 simulated key structure. */
typedef struct _key {
    const char *name;		/* descriptive name for debug */
    Widget widget;		/* Xt widget */
    Pixmap normal_pixmap;	/* normal (unpressed) pixmap */
    Pixmap pressed_pixmap;	/* pressed pixmap */
    XtIntervalId timeout_id;	/* un-press timeout ID, or 0 if none pending */
    void (*backend)(struct _key *); /* backend */
} kpkey_t;
kpkey_t rel_key;		/* REL key */
kpkey_t feed_key;		/* FEED key */
kpkey_t save_key;		/* SAVE key */
kpkey_t drop_key;		/* DROP key */

typedef void (*key_backend_t)(kpkey_t *);
static void key_init(kpkey_t *key, const char *name, Widget container,
	Position x, Position y, char *normal_pixmap_src[],
	char *pressed_pixmap_src[], key_backend_t backend);

/* Application resources. */
typedef struct {
    Pixel	foreground;
    Pixel	background;
    Pixel	cabinet;
    Pixel	cardcolor;
    Pixel	errcolor;
    char	*ifontname;
    char	*charset;
    char	*card;
    char	*demofile;
    Boolean	autonumber;
    Boolean	typeahead;
    Boolean 	remotectl;
    Boolean	empty;
    Boolean	read;
    Boolean	help;
    Boolean 	debug;
    Boolean	version;
} AppRes, *AppResptr;

static AppRes appres;

/* Command-line options. */
static XrmOptionDescRec options[]= {
    { "-ifont",		".ifont",	XrmoptionSepArg,	NULL },
    { "-nonumber",	".autoNumber",  XrmoptionNoArg,		"False" },
    { "-number",	".autoNumber",  XrmoptionNoArg,		"True" },
    { "-typeahead",	".typeahead",	XrmoptionNoArg,		"True" },
    { "-charset",	".charset",	XrmoptionSepArg,	NULL },
    { "-card",		".card",	XrmoptionSepArg,	NULL },
    { "-demo",		".demoFile",	XrmoptionSepArg,	NULL },
    { "-remotectl",	".remoteCtl",	XrmoptionNoArg,		"True" },
    { "-noread",	".read",	XrmoptionNoArg,		"False" },
    { "-empty",		".empty", 	XrmoptionNoArg,		"True" },
    { "-026ftn",	".charset",	XrmoptionNoArg,		"bcd-h" },
    { "-026comm",	".charset",	XrmoptionNoArg,		"bcd-a" },
    { "-029",		".charset",	XrmoptionNoArg,		"029" },
    { "-EBCDIC",	".charset",	XrmoptionNoArg,		"ebcdic" },
    { "-debug",		".debug",	XrmoptionNoArg,		"True" },
    { "-help",		".help",	XrmoptionNoArg,		"True" },
    { "-v",		".version",	XrmoptionNoArg,		"True" },
};

/* Resource list. */
#define offset(field) XtOffset(AppResptr, field)
static XtResource resources[] = {
    { XtNforeground, XtCForeground, XtRPixel, sizeof(Pixel),
      offset(foreground), XtRString, "XtDefaultForeground" },
    { XtNbackground, XtCBackground, XtRPixel, sizeof(Pixel),
      offset(background), XtRString, "XtDefaultBackground" },
    { "cabinet", "Cabinet", XtRPixel, sizeof(Pixel),
      offset(cabinet), XtRString, "grey75" },
    { "cardColor", "CardColor", XtRPixel, sizeof(Pixel),
      offset(cardcolor), XtRString, "ivory" },
    { "errColor", "ErrColor", XtRPixel, sizeof(Pixel),
      offset(errcolor), XtRString, "firebrick" },
    { "ifont", "IFont", XtRString, sizeof(String),
      offset(ifontname), XtRString, 0 },
    { "autoNumber", "AutoNumber", XtRBoolean, sizeof(Boolean),
      offset(autonumber), XtRString, "False" },
    { "typeahead", "Typeahead", XtRBoolean, sizeof(Boolean),
      offset(typeahead), XtRString, "True" },
    { "charset", "Charset", XtRString, sizeof(String),
      offset(charset), XtRString, NULL },
    { "card", "Card", XtRString, sizeof(String),
      offset(card), XtRString, NULL },
    { "demoFile", "DemoFile", XtRString, sizeof(String),
      offset(demofile), XtRString, NULL },
    { "remoteCtl", "RemoteCtl", XtRBoolean, sizeof(Boolean),
      offset(remotectl), XtRString, "False" },
    { "read", "Read", XtRBoolean, sizeof(Boolean),
      offset(read), XtRString, "True" },
    { "empty", "Empty", XtRBoolean, sizeof(Boolean),
      offset(empty), XtRString, "False" },
    { "debug", "Debug", XtRBoolean, sizeof(Boolean),
      offset(debug), XtRString, "False" },
    { "help", "Help", XtRBoolean, sizeof(Boolean),
      offset(help), XtRString, "False" },
    { "version", "Version", XtRBoolean, sizeof(Boolean),
      offset(version), XtRString, "False" },
};
#undef offset

/* Fallback resources. */
static String fallbacks[] = {
    "*ifont:		7x13",
    "*stackerDepression.background:	grey38",
    "*depression.background:		grey38",
    "*stacker.font:	6x13bold",
    "*stacker.foreground:	black",
    "*stacker.background:		grey92",
    "*dialog*value*font: fixed",
    "*base.background:	grey57",
    "*switch.font:  	6x10",
    "*switch.background:  		grey92",
    "*font:		variable",
    "*cabinet:				grey75",
    "*channel.background:		grey92",
    "*cardColor:	ivory", /* nonsense? */
    /*"*keybox.background:		grey92",
    "*keybox.borderColor: ivory1", */
    "*keybox.background: 		ivory1",
    "*panel.background:			grey92",
    "*keyboard.background:		grey10",
    "*deskTop.background:		white",
    "*deskEdge.background:		white",
    "*save.dialog.background:		grey92",
    NULL
};

/* Xt actions (xxx_action). */
static void Data_action(Widget, XEvent *, String *, Cardinal *);
static void MultiPunchData_action(Widget, XEvent *, String *, Cardinal *);
static void DeleteWindow_action(Widget, XEvent *, String *, Cardinal *);
static void Home_action(Widget, XEvent *, String *, Cardinal *);
static void Left_action(Widget, XEvent *, String *, Cardinal *);
static void Release_action(Widget, XEvent *, String *, Cardinal *);
static void Redraw_action(Widget, XEvent *, String *, Cardinal *);
static void Right_action(Widget, XEvent *, String *, Cardinal *);
static void Tab_action(Widget, XEvent *, String *, Cardinal *);

/* Xt callbacks. */
static void key_press(Widget, XtPointer, XtPointer);

/* Actions. */
static XtActionsRec actions[] = {
    { "Data",		Data_action },
    { "MultiPunchData", MultiPunchData_action },
    { "DeleteWindow",	DeleteWindow_action },
    { "Home",		Home_action },
    { "Left",		Left_action },
    { "Release",	Release_action },
    { "Redraw",		Redraw_action },
    { "Right",		Right_action },
    { "Tab",		Tab_action },
    { "InsertSelection", InsertSelection_action },
    { "Confirm",	Confirm_action },
    { "Hover",		Hover_action },
    { "Hover2",		Hover2_action },
    { "UnHover",	UnHover_action },
};
static int actioncount = XtNumber(actions);

static bool power_on = false;
typedef enum {
    C_EMPTY,		/* No card in punch station, no operation pending */
    C_FLUX,		/* Operation in progress (in or out) */
    C_REGISTERED	/* Card registered in punch station */
} cstate_t;
cstate_t punch_state = C_EMPTY;
#define CARD_REGISTERED	(punch_state == C_REGISTERED)

/*
 * ps_card points to the card in (or near) the punch station.
 *  It is NULL if no cards have been fed yet.
 * rs_card points to the card in the read station. It is NULL if we don't have
 *  a read station, or if the read station is currently empty.
 * stack points to the stack of cards that have passed through the punch and
 *  (possibly) read stations. stack_last is the last card in the stacker.
 */
static card_t *ps_card, *rs_card;
static card_t *stack = NULL;
static card_t *stack_last = NULL;

/* Key press back-ends (xxx_backend). */
static void save_key_backend(kpkey_t *key);
static void drop_key_backend(kpkey_t *key);
static void rel_key_backend(kpkey_t *);
static void feed_key_backend(kpkey_t *);

/* Other forward references. */
static void define_widgets(void);
static void startup_power_feed(void);
static void startup_power(void);
static void do_feed(bool keep_sequence);
static void enq_delay(void);
static void do_release(int delay);
static void do_clear_read(void);
static void show_key_down(kpkey_t *key);
static void display_card_count(void);
static void init_fsms();

/* Syntax. */
void
usage(void)
{
    charset_t cs = NULL;
    cardimg_t ci = NULL;

    fprintf(stderr, "Usage: %s [x029-options] [Xt-options]\n", programname);
    fprintf(stderr, "x029-options:\n\
  -ifont <font>    Interpreter (card edge) font, defaults to 7x13\n\
  -number          Automatically number cards in cols 73..80\n\
  -charset <name>  Keypunch character set:\n");
    for (cs = next_charset(NULL); cs != NULL; cs = next_charset(cs)) {
	fprintf(stderr, "    %-9s %s%s\n",
		charset_name(cs), charset_desc(cs),
		(cs == default_charset())? " (default)": "");
    }
    fprintf(stderr, "\
  -card <name>     Card image:\n");
    for (ci = next_cardimg(NULL); ci != NULL; ci = next_cardimg(ci)) {
	fprintf(stderr, "    %-9s %s%s\n", cardimg_name(ci), cardimg_desc(ci),
		(ci == default_cardimg())? " (default)": "");
    }
    fprintf(stderr, "\
  -026ftn          Alias for '-charset bcd-h'\n\
  -026comm         Alias for '-charset bcd-a'\n\
  -029             Alias for '-charset 029'\n\
  -EBCDIC          Alias for '-charset ebcdic'\n\
  -demo <file>     Read text file and punch it (automated display)\n\
  -demo -          Read stdin and punch it\n\
  -remotectl       Read stdin incrementally\n\
  -empty           Don't feed in a card at start-up\n\
  -noread          Don't display the read station\n\
  -debug           Write debug into to stdout\n\
  -help            Display this text\n\
  -v               Display version number and exit\n");
    exit(1);
}

int
main(int argc, char *argv[])
{
    XtTranslations table;
    Pixmap icon;

    /* Figure out who we are */
    programname = strrchr(argv[0], '/');
    if (programname) {
	++programname;
    } else {
	programname = argv[0];
    }

    /* Initialize Xt and fetch our resources. */
    toplevel = XtVaAppInitialize(
	&appcontext,
	"X029",
	options, XtNumber(options),
	&argc, argv,
	fallbacks,
	XtNinput, True,
	XtNallowShellResize, False,
	NULL);
    if (argc > 1) {
	usage();
    }
    XtGetApplicationResources(toplevel, &appres, resources,
	XtNumber(resources), 0, 0);
    if (appres.help) {
	usage();
    }
    if (appres.version) {
	fprintf(stderr, "%s\n", VERSION);
	exit(0);
    }

    /* Set up some globals. */
    display = XtDisplay(toplevel);
    default_screen = DefaultScreen(display);
    root_window = RootWindow(display, default_screen);
    depth = DefaultDepthOfScreen(XtScreen(toplevel));
    a_delete_me = XInternAtom(display, "WM_DELETE_WINDOW", False);

    /* Set up actions. */
    XtAppAddActions(appcontext, actions, actioncount);

    /* Load fonts. */
    ifontinfo = XLoadQueryFont(display, appres.ifontname);
    if (ifontinfo == NULL) {
	XtError("Can't load interpreter font");
    }

    /* Pick out the character set. */
    if (appres.charset != NULL) {
	ccharset = find_charset(appres.charset);
	if (ccharset == NULL) {
	    ccharset = default_charset();
	    fprintf(stderr, "No such charset: '%s', defaulting to '%s'\n"
			    "Use '-help' to list the available character "
			    "sets\n",
			    appres.charset, charset_name(ccharset));
	    }
    } else {
	ccharset = default_charset();
    }

    if (appres.demofile != NULL) {
	if (appres.remotectl) {
	    fprintf(stderr, "Demofile and remotectl in conflict, "
			    "ignoring remotectl\n");
	}
	if (strcmp(appres.demofile, "-")) {
	    ap_fd = open(appres.demofile, O_RDONLY | O_NONBLOCK);
	    if (ap_fd < 0) {
		perror(appres.demofile);
		exit(1);
	    }
	} else {
	    ap_fd = fileno(stdin);
	}
	mode = M_BATCH;
    } else if (appres.remotectl) {
	mode = M_REMOTECTL;
	ap_fd = fileno(stdin);
    } else {
	mode = M_INTERACTIVE;
    }

    /* Define the widgets. */
    define_widgets();

    /* Set up a cute (?) icon. */
    icon = XCreateBitmapFromData(display, XtWindow(toplevel),
	(char *)x029_bits, x029_width, x029_height);
    XtVaSetValues(toplevel, XtNiconPixmap, icon, XtNiconMask, icon, NULL);

    /* Allow us to die gracefully. */
    XSetWMProtocols(display, XtWindow(toplevel), &a_delete_me, 1);
    table = XtParseTranslationTable("<Message>WM_PROTOCOLS: DeleteWindow()");
    XtOverrideTranslations(toplevel, table);

#if defined(SOUND) /*[*/
    /* Set up clicks. */
    audio_init();
#endif /*]*/

    if (mode != M_INTERACTIVE && ap_fd == fileno(stdin)) {
	if (fcntl(ap_fd, F_SETFL, fcntl(ap_fd, F_GETFL) | O_NONBLOCK) < 0) {
	    perror("fcntl");
	    exit(1);
	}
    }

    if ((mode == M_INTERACTIVE || mode == M_REMOTECTL) && !appres.empty) {
	startup_power_feed();
    } else {
	startup_power();
    }

    /* Init the paste and auto-play FSMs. */
    init_fsms();

    /* Process X events forever. */
    XtAppMainLoop(appcontext);

    return 0;
}

static void
power_off_timeout(XtPointer data, XtIntervalId *id)
{
    exit(0);
}

static void
do_power_off(void)
{
    power_on = false;
    XtVaSetValues(power_widget, XtNbackgroundPixmap, flipper_off, NULL);
    (void) XtAppAddTimeOut(appcontext, VERY_SLOW * 2, power_off_timeout, NULL);
}

/* Callback for power button. */
static void
power_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
    dbg_printf("[callback] power\n");
    do_power_off();
}

void
queued_OFF(unsigned char ignored)
{
    do_power_off();
}

/* Definitions for the toggle switches. */

static Pixmap toggle_on, toggle_off;

struct toggle {
    Widget w;
    int on;
};
struct toggle toggles[8];

/* Timeout function for the CLEAR switch. */
static void
unclear_event(XtPointer data, XtIntervalId *id)
{
    toggles[T_CLEAR].on = 0;
    XtVaSetValues(toggles[T_CLEAR].w, XtNbackgroundPixmap, toggle_off, NULL);
}

/* CLEAR switch function. */
static void
clear_switch(void)
{
    dbg_printf("[callback] clear\n");
    XtVaSetValues(toggles[T_CLEAR].w, XtNbackgroundPixmap, toggle_on, NULL);
    (void) XtAppAddTimeOut(appcontext, SLOW * 6, unclear_event, NULL);

    if (CARD_REGISTERED) {
	do_release(VERY_FAST);
	if (appres.read) {
	    do_clear_read();
	}
    } else if (punch_state == C_EMPTY && rs_card != NULL) {
	do_clear_read();
    }
}

/* Callback function for toggle switches. */
static void
toggle_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
    struct toggle *t = (struct toggle *)client_data;

    if (t != &toggles[T_CLEAR]) {
	t->on = !t->on;
	XtVaSetValues(w, XtNbackgroundPixmap, t->on? toggle_on: toggle_off,
		NULL);
	return;
    }

    /* It's the CLEAR switch. */
    if (t->on) {
	return;
    }
    t->on = !t->on;

    clear_switch();
}

/* Turn off the auto-feed switch. */
static void
auto_feed_off(void)
{
    XtVaSetValues(toggles[T_AUTO_FEED].w, XtNbackgroundPixmap, toggle_off,
	    NULL);
}

/* Card-image data structures. */

static int col = 0;
static GC gc, invgc, holegc;

static Widget container, ps_cardw, rs_cardw, posw_porth, posw;
#define FEED_X	(ps_offset + (card_width * 2 / 3) - CELL_X(SLAM_COL))
#define FEED_Y 	(CHANNEL_TFRAME - 3 - CARD_HEIGHT)

static Dimension card_width, card_height;
static Dimension hole_width, hole_height;

typedef struct {
    cardimg_t c;
    Pixmap p;
} pxcache_t;
static pxcache_t *pxcache = NULL;
static int pxcache_count = 0;

static Pixmap
pixmap_for_cardimg(cardimg_t c, Pixmap p0)
{
    int i;
    Pixmap p, shapemask;
    XpmAttributes attributes;

    for (i = 0; i < pxcache_count; i++) {
	if (pxcache[i].c == c) {
	    return pxcache[i].p;
	}
    }

    if (p0 != 0) {
	p = p0;
    } else {
	attributes.valuemask = XpmSize;
	if (XpmCreatePixmapFromData(display, XtWindow(container),
		    cardimg_pixmap_source(c), &p, &shapemask,
		    &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
	}
    }
    pxcache = (pxcache_t *)XtRealloc((XtPointer)pxcache,
	    (pxcache_count + 1) * sizeof(pxcache_t));
    pxcache[pxcache_count].c = c;
    pxcache[pxcache_count].p = p;
    pxcache_count++;
    return p;
}

/* Set up the keypunch. */
static void
define_widgets(void)
{
    Dimension w, h;
    Position posw_x;
    XtTranslations t;
    XGCValues xgcv;
    int i;
    Pixmap pixmap, shapemask;
    Pixmap hole_shapemask;
    Pixmap column_indicator;
    Pixmap arrow;
    Position sx;
    Position sp1, sp2;
    Widget base;
    Widget channel;
    Widget keybox;
    Widget keyboard;
    XpmAttributes attributes;
    static char translations[] = "\
	<Key>Left:	Left()\n\
	<Key>BackSpace:	Left()\n\
	<Key>Right:	Right()\n\
	<Key>Home:	Home()\n\
	<Key>Return:	Release()\n\
	<Key>KP_Enter:	Home()\n\
	<Key>Down:	Release()\n\
	<Key>Tab:	Tab()\n\
	<Btn2Down>:	InsertSelection(PRIMARY)\n\
	Alt<Key>:	MultiPunchData()\n\
	Meta<Key>:	MultiPunchData()\n\
	Ctrl<Key>v:	InsertSelection(CLIPBOARD)\n\
	<Key>:		Data()\n";

    /* Create a container for the whole thing. */
    container = XtVaCreateManagedWidget(
	"container", compositeWidgetClass, toplevel,
	XtNwidth, 10,	/* temporary dimensions */
	XtNheight, 10,
	XtNbackground, appres.cabinet,
	NULL);
    XtRealizeWidget(toplevel);

    /* Figure out the card image. */
    if (appres.card != NULL) {
	if ((ccardimg = find_cardimg(appres.card)) == NULL) {
	    ccardimg = default_cardimg();
	    fprintf(stderr, "No such card '%s', defaulting to '%s'\n"
			    "Use '-help' to list the types\n",
			    appres.card, cardimg_name(ccardimg));
	}
    } else {
	ccardimg = default_cardimg();
    }

    attributes.valuemask = XpmSize;
    if (XpmCreatePixmapFromData(display, XtWindow(container),
		cardimg_pixmap_source(ccardimg), &pixmap, &shapemask,
		&attributes) != XpmSuccess) {
	XtError("XpmCreatePixmapFromData failed");
    }
    (void) pixmap_for_cardimg(ccardimg, pixmap);

    card_width = attributes.width;
    card_height = attributes.height;
    attributes.valuemask = XpmSize;
    if (XpmCreatePixmapFromData(display, XtWindow(container), hole,
		&hole_pixmap, &hole_shapemask, &attributes) != XpmSuccess) {
	XtError("XpmCreatePixmapFromData failed");
    }
    hole_width = attributes.width;
    hole_height = attributes.height;
    w = card_width + 2*CARD_AIR;
#if 0
    h = BUTTON_GAP + POSW_HEIGHT +
	SWITCH_SKIP + card_height + 2*CARD_AIR + 2*BUTTON_GAP + 2*BUTTON_BW +
	BUTTON_HEIGHT + POWER_GAP + CARDIMG_MENU_HEIGHT;
#endif
    h = TOTAL_HEIGHT;
    if (appres.read) {
	ps_offset = w;
    } else {
	ps_offset = 0;
    }

    /* Add the stacker count. */
    XtVaCreateManagedWidget(
	"stackerDepression", labelWidgetClass, container,
	XtNwidth, STACKER_WIDTH + 2 * POSW_FRAME,
	XtNheight, POSW_HEIGHT,
	XtNx, 0,
	XtNy, 0,
	XtNlabel, "",
	XtNborderWidth, 0,
	NULL);
    stacker = XtVaCreateManagedWidget(
	"stacker", labelWidgetClass, container,
	XtNwidth, STACKER_WIDTH,
	XtNheight, POSW_INNER_HEIGHT,
	XtNx, POSW_FRAME,
	XtNy, POSW_TFRAME,
	XtNborderWidth, 0,
	XtNlabel, "",
	XtNresize, False,
	NULL);
    display_card_count();

    /* Add the position counter. */
    if (XpmCreatePixmapFromData(display, XtWindow(container),
		ci2_xpm, &column_indicator, &shapemask,
		&attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    if (appres.read) {
	posw_x = (card_width - (POSW_WIDTH + ARROW_WIDTH)) / 2;
    } else {
	posw_x = STACKER_WIDTH + 2 * POSW_FRAME + BUTTON_GAP;
    }
    XtVaCreateManagedWidget(
	"depression", labelWidgetClass, container,
	XtNwidth, POSW_WIDTH + ARROW_WIDTH,
	XtNheight, POSW_HEIGHT,
	XtNy, 0,
	XtNx, posw_x,
	XtNlabel, "",
	XtNborderWidth, 0,
	NULL);
    posw_porth = XtVaCreateManagedWidget(
	"posw_porthole", portholeWidgetClass, container,
	XtNwidth, POSW_INNER_WIDTH,
	XtNheight, POSW_INNER_HEIGHT,
	XtNx, posw_x + POSW_FRAME + ARROW_WIDTH,
	XtNy, POSW_TFRAME,
	XtNborderWidth, 0,
	NULL);
    posw = XtVaCreateManagedWidget(
	"posw", compositeWidgetClass, posw_porth,
	XtNwidth, 1350,
	XtNheight, POSW_HEIGHT,
	XtNx, 0,
	XtNy, 0,
	XtNbackgroundPixmap, column_indicator,
	XtNborderWidth, 1,
	XtNborderColor, appres.background,
	NULL);
    if (XpmCreatePixmapFromData(display, XtWindow(container), arrow_xpm,
		&arrow, &shapemask, &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    (void) XtVaCreateManagedWidget(
	"arrow", compositeWidgetClass, container,
	XtNwidth, ARROW_WIDTH,
	XtNheight, POSW_INNER_HEIGHT,
	XtNx, posw_x + POSW_FRAME,
	XtNy, POSW_TFRAME,
	XtNbackgroundPixmap, arrow,
	XtNborderWidth, 0,
	NULL);

    /* Add the channel. */
    channel = XtVaCreateManagedWidget(
	"channel", compositeWidgetClass, container,
	XtNwidth, ps_offset + w,
	XtNheight, CHANNEL_HEIGHT,
	XtNx, CHANNEL_X,
	XtNy, CHANNEL_Y,
	XtNborderWidth, 0,
	NULL);

    /* Create the cards. */
    ps_cardw = XtVaCreateManagedWidget(
	"card", compositeWidgetClass, channel,
	XtNwidth, card_width,
	XtNheight, card_height,
	XtNx, FEED_X,
	XtNy, -(card_height + CARD_AIR),
	XtNborderWidth, 0,
	XtNbackgroundPixmap, pixmap,
	NULL);
    rs_cardw = XtVaCreateManagedWidget(
	"card", compositeWidgetClass, channel,
	XtNwidth, card_width,
	XtNheight, card_height,
	XtNx, FEED_X,
	XtNy, -(card_height + CARD_AIR),
	XtNborderWidth, 0,
	XtNbackgroundPixmap, pixmap,
	NULL);

    /* Add the desktop behind the keybox. */
    XtVaCreateManagedWidget(
	"deskTop", compositeWidgetClass, container,
	XtNwidth, ps_offset - 1,
	XtNheight, KEYBOX_HEIGHT,
	XtNx, -1,
	XtNy, KEYBOX_Y,
	XtNborderWidth, 1,
	NULL);

    /* Add the keyboard case. */
    keybox = XtVaCreateManagedWidget(
	"keybox", compositeWidgetClass, container,
	XtNwidth, KEYBOX_WIDTH,
	XtNheight, KEYBOX_HEIGHT,
	XtNx, ps_offset,
	XtNy, KEYBOX_Y,
	XtNborderWidth, 0,
	NULL);

    /* 'sx' is where the leftmost key starts. */
    sx = (KEYBOX_WIDTH - 8*SWITCH_WIDTH - 7*BUTTON_GAP) / 2;

    /* Add the silver panels behind the switches. */
    sp1 = ((SWITCH_WIDTH + BUTTON_GAP) * 3) / 2;
    XtVaCreateManagedWidget(
	"panel", compositeWidgetClass, keybox,
	XtNx, KEYBOX_BORDER,
	XtNy, KEYBOX_BORDER,
	/*XtNwidth, KEYBOX_WIDTH - 2*KEYBOX_BORDER - 2,*/
	XtNwidth, sx + sp1 - 2,
	XtNheight, SWITCHES_HEIGHT - 2,
	XtNborderWidth, 1,
	NULL);
    sp2 = (SWITCH_WIDTH + BUTTON_GAP) * 5;
    XtVaCreateManagedWidget(
	"panel", compositeWidgetClass, keybox,
	XtNx, KEYBOX_BORDER + sx + sp1 - 1,
	XtNy, KEYBOX_BORDER,
	XtNwidth, sp2 - 2,
	XtNheight, SWITCHES_HEIGHT - 2,
	XtNborderWidth, 1,
	NULL);
    XtVaCreateManagedWidget(
	"panel", compositeWidgetClass, keybox,
	XtNx, KEYBOX_BORDER + sx + sp1 + sp2 - 2,
	XtNy, KEYBOX_BORDER,
	XtNwidth, KEYBOX_WIDTH - 2*KEYBOX_BORDER - (sx + sp1 + sp2),
	XtNheight, SWITCHES_HEIGHT - 2,
	XtNborderWidth, 1,
	NULL);

    /* Add the switches. */
    if (XpmCreatePixmapFromData(display, XtWindow(container), off60_xpm,
		    &toggle_off, &shapemask, &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    if (XpmCreatePixmapFromData(display, XtWindow(container), on60_xpm,
		    &toggle_on, &shapemask, &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    for (i = 0; i < 8; i++) {
	if (i == 1 || i == 6) {
	    continue;
	}
	toggles[i].on = (i < 7);
	toggles[i].w = XtVaCreateManagedWidget(
	    "switchcmd", commandWidgetClass, keybox,
	    XtNwidth, SWITCH_WIDTH,
	    XtNx, sx + i*(SWITCH_WIDTH + BUTTON_GAP),
	    XtNy, SWITCHES_TFRAME + 5,
	    XtNheight, SWITCH_HEIGHT,
	    XtNborderWidth, 0,
	    XtNlabel, "",
	    XtNbackgroundPixmap, toggles[i].on? toggle_on: toggle_off,
	    XtNhighlightThickness, 0,
	    NULL);
	XtAddCallback(toggles[i].w, XtNcallback, toggle_callback,
		&toggles[i]);
	(void) XtVaCreateManagedWidget(
	    "switch", labelWidgetClass, keybox,
	    XtNwidth, SWITCH_WIDTH,
	    XtNx, sx + i*(SWITCH_WIDTH + BUTTON_GAP),
	    XtNy, SWITCHES_TFRAME - 5,
	    XtNborderWidth, 0,
	    XtNlabel, top_label[i],
	    NULL);
	(void) XtVaCreateManagedWidget(
	    "switch", labelWidgetClass, keybox,
	    XtNwidth, SWITCH_WIDTH,
	    XtNx, sx + i*(SWITCH_WIDTH + BUTTON_GAP),
	    XtNy, SWITCHES_TFRAME + 5 + SWITCH_HEIGHT,
	    XtNborderWidth, 0,
	    XtNlabel, bottom_label1[i],
	    NULL);
	if (bottom_label2[i] != NULL)
	    (void) XtVaCreateManagedWidget(
		"switch", labelWidgetClass, keybox,
		XtNwidth, SWITCH_WIDTH,
		XtNx, sx + i*(SWITCH_WIDTH + BUTTON_GAP),
		XtNy, SWITCHES_TFRAME + 5 + SWITCH_HEIGHT + 10,
		XtNborderWidth, 0,
		XtNlabel, bottom_label2[i],
		NULL);
	if (bottom_label3[i] != NULL)
	    (void) XtVaCreateManagedWidget(
		"switch", labelWidgetClass, keybox,
		XtNwidth, SWITCH_WIDTH,
		XtNx, sx + i*(SWITCH_WIDTH + BUTTON_GAP),
		XtNy, SWITCHES_TFRAME + 5 + SWITCH_HEIGHT + 20,
		XtNborderWidth, 0,
		XtNlabel, bottom_label3[i],
		NULL);
    }

    /* Add the keyboard area. */
    keyboard = XtVaCreateManagedWidget(
	"keyboard", compositeWidgetClass, keybox,
	XtNwidth, KEYBOARD_WIDTH,
	XtNheight, KEYBOARD_HEIGHT,
	XtNx, KEYBOARD_X,
	XtNy, KEYBOARD_Y,
	XtNborderWidth, 0,
	NULL);
    keyboard = keyboard; /* for now */

    /* Add the SAVE key to the keyboard. */
    key_init(&save_key, "SAVE", keyboard,
	    KEYBOARD_LRFRAME, /* x */
	    KEYBOARD_TFRAME,  /* y */
	    save_xpm, save_pressed_xpm,
	    save_key_backend);

    /* Add the DROP key to the keyboard. */
    key_init(&drop_key, "DROP", keyboard,
	    KEYBOARD_LRFRAME + KEY_WIDTH, /* x */
	    KEYBOARD_TFRAME,  /* y */
	    drop_xpm, drop_pressed_xpm,
	    drop_key_backend);

    /* Add the FEED key to the keyboard. */
    key_init(&feed_key, "FEED", keyboard,
	    KEYBOARD_WIDTH - KEYBOARD_LRFRAME - KEY_WIDTH, /* x */
	    KEYBOARD_TFRAME,  /* y */
	    feed_xpm, feed_pressed_xpm,
	    feed_key_backend);

    /* Add the REL key to the keyboard. */
    key_init(&rel_key, "REL", keyboard,
	    KEYBOARD_WIDTH - KEYBOARD_LRFRAME - KEY_WIDTH - KEY_WIDTH, /* x */
	    KEYBOARD_TFRAME,  /* y */
	    rel_xpm, rel_pressed_xpm,
	    rel_key_backend);

    /* Create the desk edge. */
    XtVaCreateManagedWidget(
	"deskEdge", compositeWidgetClass, container,
	XtNwidth, ps_offset + w,
	XtNheight, DESK_THICKNESS,
	XtNx, -1,
	XtNy, DESK_Y,
	XtNborderWidth, DESK_FRAME,
	NULL);

    /* Create the base. */
    base = XtVaCreateManagedWidget(
	"base", compositeWidgetClass, container,
	XtNwidth, ps_offset + w,
	XtNheight, CARDIMG_MENU_HEIGHT + 2*CARD_AIR,
	XtNx, -1,
	XtNy, BASE_Y,
	NULL);

    /* Add the power button to the base. */
    if (XpmCreatePixmapFromData(display, XtWindow(container), flipper_on_xpm,
		    &flipper_on, &shapemask, &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    if (XpmCreatePixmapFromData(display, XtWindow(container), flipper_off_xpm,
		    &flipper_off, &shapemask, &attributes) != XpmSuccess) {
	    XtError("XpmCreatePixmapFromData failed");
    }
    power_widget = XtVaCreateManagedWidget(
	"power", commandWidgetClass, base,
	XtNbackgroundPixmap, flipper_off,
	XtNlabel, "",
	XtNwidth, POWER_WIDTH,
	XtNheight, POWER_HEIGHT,
	XtNx, ps_offset + w - (CARD_AIR + POWER_WIDTH),
	XtNy, CARD_AIR,
	XtNborderWidth, 0,
	XtNhighlightThickness, 0,
	NULL
    );
    XtAddCallback(power_widget, XtNcallback, power_callback, NULL);

    /* Create the character and card image menus on the base. */
    charset_menu_init(ccharset, base,
	    CARD_AIR,	/* x */
	    CARD_AIR);	/* y */
    cardimg_menu_init(ccardimg, base,
	    CARD_AIR + CARDIMG_MENU_WIDTH + CARD_AIR, /* x */
	    CARD_AIR);	/* y */

    /* Create graphics contexts for drawing. */
    xgcv.foreground = appres.foreground;
    xgcv.background = appres.cardcolor;
    xgcv.font = ifontinfo->fid;
    gc = XtGetGC(toplevel, GCForeground|GCBackground|GCFont, &xgcv);
    xgcv.foreground = appres.cardcolor;
    xgcv.background = appres.foreground;
    xgcv.font = ifontinfo->fid;
    invgc = XtGetGC(toplevel, GCForeground|GCBackground|GCFont, &xgcv);
    xgcv.tile = hole_pixmap;
    xgcv.fill_style = FillTiled;
    holegc = XtGetGC(toplevel, GCTile|GCFillStyle, &xgcv);

    /* Fix the size of the toplevel window. */
    XtVaSetValues(toplevel,
	XtNwidth, ps_offset + w,
	XtNheight, h,
	XtNbaseWidth, ps_offset + w,
	XtNbaseHeight, h,
	XtNminWidth, ps_offset + w,
	XtNminHeight, h,
	XtNmaxWidth, ps_offset + w,
	XtNmaxHeight, h,
	NULL);

    /* Define event translations. */
    t = XtParseTranslationTable(translations);
    XtOverrideTranslations(container, t);
    t = XtParseTranslationTable("<Expose>: Redraw()");
    XtOverrideTranslations(ps_cardw, t);
    XtOverrideTranslations(rs_cardw, t);

    /* Inflate it all. */
    XtRealizeWidget(toplevel);
}

/* Punch a character into a particular column of the card the punch station. */
static bool
punch_char(int cn, unsigned char c)
{
    int j;

    if (charset_xlate(ccharset, c) == NS) {
	/* Map lowercase, to be polite. */
	if (islower(c) && charset_xlate(ccharset, toupper(c)) != NS) {
	    c = toupper(c);
	} else {
	    return false;
	}
    }

    /* Space?  Do nothing. */
    if (!charset_xlate(ccharset, c)) {
	return true;
    }

    ps_card->holes[cn] |= charset_xlate(ccharset, c);

    /* Redundant? */
    for (j = 0; j < ps_card->n_ov[cn]; j++) {
	if (ps_card->coltxt[cn][j] == c) {
	    return true;
	}
    }

    if (toggles[T_PRINT].on) {
	if (ps_card->n_ov[cn] < N_OV) {
	    ps_card->coltxt[cn][ps_card->n_ov[cn]] = c;
	    ++ps_card->n_ov[cn];
	}
    }

    return true;
}

/* Render the image of a card column onto the X display. */
static void
draw_col(card_t *card, int window, int cn)
{
    int i;
    int j;
    int x = LEFT_PAD + CELL_X(cn);

#if defined(XXDEBUG) /*[*/
    printf(" draw_col(col %d)\n", cn);
#endif /*]*/

    /* Draw the text at the top, possibly overstruck. */
    for (j = 0; j < card->n_ov[cn]; j++) {
	if (card->coltxt[cn][j] < ' ') {
	    continue;
	}
	XDrawString(display, window, gc, x, TOP_PAD + TEXT_PAD,
		(char *)&card->coltxt[cn][j], 1);
    }

    /* Draw the holes, top to bottom. */
    for (i = 0; i < N_ROWS; i++) {
	if (card->holes[cn] & (0x800>>i)) {
	    XGCValues xgcv;

	    xgcv.ts_x_origin = x;
	    xgcv.ts_y_origin = TOP_PAD + HOLE_PAD + (CELL_Y(i));
	    XChangeGC(display, holegc, GCTileStipXOrigin|GCTileStipYOrigin,
		    &xgcv);

	    XFillRectangle(display, window, holegc, x,
		    TOP_PAD + HOLE_PAD + (CELL_Y(i)), hole_width, hole_height);
	}
    }
}

/* Update the column indicator in the lower right. */
static void
set_posw(int c)
{
    col = c;

    if (col < N_COLS) {
	XtVaSetValues(posw, XtNx, -(col * 14), NULL);
    }
}

/* Go to the next card. */
void
queued_NEWCARD(unsigned char replace)
{
    int i;

    /* Change the card image. */
    if (ncardimg != NULL && ccardimg != ncardimg) {
	ccardimg = ncardimg;
	ncardimg = NULL;
    }
    XtVaSetValues(ps_cardw, XtNbackgroundPixmap,
	    pixmap_for_cardimg(ccardimg, 0), NULL);

    if (!ps_card) {
	/* Allocate a new card. */
	ps_card = (card_t *)XtMalloc(sizeof(card_t));
	ps_card->next = NULL;

	ps_card->seq = line_number;
	line_number += 10;
    } else if (mode != M_INTERACTIVE) {

	/* In auto-play mode, increment the sequence number. */
	ps_card->seq = line_number;
	line_number += 10;
    }

    /* Clean out the new card. */
    ps_card->cardimg = ccardimg;
    ps_card->charset = ccharset;
    (void) memset(ps_card->coltxt, ' ', sizeof(ps_card->coltxt));
    (void) memset(ps_card->holes, 0, sizeof(ps_card->holes));
    (void) memset(ps_card->n_ov, 0, sizeof(ps_card->n_ov));
    if (appres.autonumber) {
	char ln_buf[9];

	(void) sprintf(ln_buf, "%08d", ps_card->seq);
	for (i = 0; i < 8; i++) {
	    punch_char(72+i, ln_buf[i]);
	}
    }
}

/* Redraw the entire card image. */
static void
Redraw_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    int i;
    Position x, y;
    Dimension w, h;
    card_t *card;

    action_dbg("Redraw", wid, event, params, num_params);

    if (event && event->type == Expose) {
	x = event->xexpose.x;
	y = event->xexpose.y;
	w = event->xexpose.width;
	h = event->xexpose.height;
    } else {
	assert(0);
	x = y = 0;
	w = card_width;
	h = card_height;
    }
    if (wid == ps_cardw) {
	assert(ps_card != NULL);
	card = ps_card;
    } else {
	assert(rs_card != NULL);
	card = rs_card;
    }

    /* Slice off the padding. */
    if (x < LEFT_PAD) {			/* Left. */
	if (w <= LEFT_PAD - x) {
#if defined(XDEBUG) /*[*/
	    printf("ignoring left\n");
#endif /*]*/
	    return;
	}
	w -= LEFT_PAD - x;
	x = 0;
    } else {
	x -= LEFT_PAD;
    }
    if (y < TOP_PAD) {			/* Top. */
	if (h <= TOP_PAD - y) {
#if defined(XDEBUG) /*[*/
	    printf("ignoring top\n");
#endif /*]*/
	    return;
	}
	h -= TOP_PAD - y;
	y = 0;
    } else {
	y -= TOP_PAD;
    }
    if (x >= (CELL_X(N_COLS))) {		/* Right. */
#if defined(XDEBUG) /*[*/
	printf("ignoring right\n");
#endif /*]*/
	return;
    }
    if (x + w > (CELL_X(N_COLS)))
	w = (CELL_X(N_COLS)) - x;
    if (y >= (CELL_Y(N_ROWS))) {		/* Bottom. */
#if defined(XDEBUG) /*[*/
	printf("ignoring left\n");
#endif /*]*/
	return;
    }
    if (y + h > (CELL_Y(N_ROWS)))
	h = (CELL_Y(N_ROWS)) - y;

    for (i = COL_FROM_X(x);
	 i < COL_FROM_X(x + w + CELL_WIDTH) && i < N_COLS;
	 i++) {
	draw_col(card, XtWindow(wid), i);
    }
}

/* Exit. */
static void
DeleteWindow_action(Widget wid, XEvent *event, String *params,
	Cardinal *num_params)
{
    action_dbg("DeleteWindow", wid, event, params, num_params);

    if (wid == toplevel) {
	exit(0);
    } else {
	XtPopdown(wid);
    }
}

/* Find the first card in the stacker. This is an external entry point. */
card_t *
first_card(void)
{
    return stack;
}

/* Return the next card, skipping the one in the punch station. */
card_t *
next_card(card_t *c)
{
    return c->next;
}

static void
save_key_backend(kpkey_t *key)
{
    if (mode == M_INTERACTIVE && power_on) {
	save_popup();
    }
}

/* Clear out the stacker after a save. */
void
clear_stacker(void)
{
    card_t *c;
    card_t *next;

    /*
     * Free everything but the cards in the punch and read stations.
     */
    for (c = stack; c != NULL; c = next) {
	next = c->next;
	free(c);
    }
    stack = NULL;
    stack_last = NULL;

    /* Update the card count display. */
    card_count = 0;
    display_card_count();
}

/*
 * Internals of functions that are enqueued with a delay.
 */

void
queued_DUMMY(unsigned char ignored)
{
}

void
queued_DATA(unsigned char c)
{
    if (CARD_REGISTERED && col < N_COLS) {
	if (punch_char(col, c)) {
	    draw_col(ps_card, XtWindow(ps_cardw), col);
#if defined(SOUND) /*[*/
	    loud_click();
#endif /*]*/
	    queued_KYBD_RIGHT(0);
	}
    }
}

void
queued_MULTIPUNCH(unsigned char c)
{
    if (col < N_COLS && punch_char(col, c)) {
	draw_col(ps_card, XtWindow(ps_cardw), col);
#if defined(SOUND) /*[*/
	loud_click();
#endif /*]*/
    }
}

void
queued_KEY_LEFT(unsigned char c)
{
    if (col) {
	queued_PAN_LEFT_BOTH(0);
	set_posw(col - 1);
    } else {
	flush_typeahead();
    }
}

/* The queued keyboard cursor-right operation. */
void
queued_KYBD_RIGHT(unsigned char do_click)
{
    if (col < N_COLS) {
	queued_PAN_RIGHT_BOTH(do_click);
	set_posw(col + 1);

	/* Do auto-feed. */
	if (toggles[T_AUTO_FEED].on && col == N_COLS) {
	    do_release(VERY_FAST);
	    do_feed(false);
	    did_auto_rel = true;
	}
    } else {
	flush_typeahead();
    }
}

/* One column's worth of queued scroll right from the REL key. */
void
queued_REL_RIGHT(unsigned char do_click)
{
    if (col < N_COLS) {
	queued_PAN_RIGHT_BOTH(do_click);
	set_posw(col + 1);
    }
}

void
queued_PAN_LEFT_PRINT(unsigned char ignored)
{
    Position x;

    XtVaGetValues(ps_cardw, XtNx, &x, NULL);
    x += CELL_WIDTH;
    XtVaSetValues(ps_cardw, XtNx, x, NULL);

#if defined(SOUND) /*[*/
    soft_click();
#endif /*]*/
}

void
queued_PAN_LEFT_BOTH(unsigned char ignored)
{
    Position x;

    XtVaGetValues(ps_cardw, XtNx, &x, NULL);
    x += CELL_WIDTH;
    XtVaSetValues(ps_cardw, XtNx, x, NULL);

    if (appres.read) {
	XtVaGetValues(rs_cardw, XtNx, &x, NULL);
	x += CELL_WIDTH;
	XtVaSetValues(rs_cardw, XtNx, x, NULL);
    }
#if defined(SOUND) /*[*/
    soft_click();
#endif /*]*/
}

/*
 * A queued pan right operation.
 * This has three purposes, which will probably need to be split:
 * - Moving a new print station card into position.
 * - Moving a print station card right when punched, scrolled or released
 *   (within the station).
 * - Moving a print station card right (when releasing).
 * The operation affects just the print station card. The second affects both
 * the print station card and the read station card.
 */
void
queued_PAN_RIGHT_BOTH(unsigned char do_click)
{
    Position x;

    XtVaGetValues(ps_cardw, XtNx, &x, NULL);
    x -= CELL_WIDTH;
    XtVaSetValues(ps_cardw, XtNx, x, NULL);

    if (appres.read) {
	XtVaGetValues(rs_cardw, XtNx, &x, NULL);
	x -= CELL_WIDTH;
	XtVaSetValues(rs_cardw, XtNx, x, NULL);
    }

#if defined(SOUND) /*[*/
    if (do_click) {
	soft_click();
    }
#endif /*]*/
}

void
queued_PAN_RIGHT_PRINT(unsigned char do_click)
{
    Position x;

    XtVaGetValues(ps_cardw, XtNx, &x, NULL);
    x -= CELL_WIDTH;
    XtVaSetValues(ps_cardw, XtNx, x, NULL);

#if defined(SOUND) /*[*/
    if (do_click) {
	soft_click();
    }
#endif /*]*/
}

void
queued_PAN_RIGHT_READ(unsigned char do_click)
{
    Position x;

    XtVaGetValues(rs_cardw, XtNx, &x, NULL);
    x -= CELL_WIDTH;
    XtVaSetValues(rs_cardw, XtNx, x, NULL);

#if defined(SOUND) /*[*/
    if (do_click) {
	soft_click();
    }
#endif /*]*/
}

void
queued_PAN_UP(unsigned char ignored)
{
    Position y;

    XtVaGetValues(ps_cardw, XtNy, &y, NULL);
    y += CELL_HEIGHT;
    XtVaSetValues(ps_cardw, XtNy, y, NULL);
}

void
queued_HOME(unsigned char ignored)
{
    queued_PAN_LEFT_BOTH(0);
    set_posw(col - 1);
}

void
queued_SLAM(unsigned char ignored)
{
    XtVaSetValues(ps_cardw,
	XtNx, FEED_X,
	XtNy, FEED_Y,
	NULL);
}

void
queued_FLUX(unsigned char ignored)
{
    punch_state = C_FLUX;
}

void
queued_REGISTERED(unsigned char ignored)
{
    punch_state = C_REGISTERED;
    set_posw(0);
}

/* Add a character to the card the punch station. */
static bool
add_char(char c)
{
    if (power_on && CARD_REGISTERED) {
	/* Make sure we will actually punch the character. */
	if (charset_xlate(ccharset, c) != NS || (islower(c) && charset_xlate(ccharset, toupper(c)) != NS)) {
	    enq_event(DATA, c, true, SLOW);
	}
	return true;
    } else {
	return false;
    }
}

static void
enq_delay(void)
{
    enq_event(DUMMY, 0, false, VERY_SLOW);
}

/*
 * Externals of delayed functions, called by the toolkit.
 */

static void
Data_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    XKeyEvent *kevent = (XKeyEvent *)event;
    char buf[10];
    KeySym ks;
    int ll;

    action_dbg("Data", wid, event, params, num_params);

    if (!power_on || !CARD_REGISTERED) {
	return;
    }

    ll = XLookupString(kevent, buf, 10, &ks, (XComposeStatus *)NULL);
    if (ll == 1) {
	enq_event(DATA, buf[0] & 0xff, true, SLOW);
    }
}

static void
MultiPunchData_action(Widget wid, XEvent *event, String *params,
	Cardinal *num_params)
{
    XKeyEvent *kevent = (XKeyEvent *)event;
    char buf[10];
    KeySym ks;
    int ll;

    action_dbg("MultiPunchData", wid, event, params, num_params);

    if (!power_on || !CARD_REGISTERED) {
	return;
    }

    ll = XLookupString(kevent, buf, 10, &ks, (XComposeStatus *)NULL);
    if (ll == 1) {
	enq_event(MULTIPUNCH, buf[0], true, SLOW);
    }
}

static void
Left_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    action_dbg("Left", wid, event, params, num_params);

    if (power_on && CARD_REGISTERED) {
	enq_event(KEY_LEFT, 0, true, SLOW);
    }
}

static void
Right_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    action_dbg("Right", wid, event, params, num_params);

    if (power_on && CARD_REGISTERED) {
	enq_event(KYBD_RIGHT, 1, true, SLOW);
    }
}

static void
Home_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    int i;

    action_dbg("Home", wid, event, params, num_params);

    if (power_on && CARD_REGISTERED) {
	flush_typeahead();
	punch_state = C_FLUX;
	for (i = 0; i < col; i++) {
	    enq_event(HOME, 0, false, FAST);
	}
	enq_event(REGISTERED, 0, false, 0);
    }
}

/*
 * Scroll the card in the punch station away, and get a new one.
 * This is mapped to the Enter (X11 'Return') key, which is the 029 REL key.
 */
static void
rel_key_backend(kpkey_t *key)
{
    dbg_printf("[callback] release(%s) eq_count = %d\n", CARD_REGISTERED? "card": "no card", eq_count);

    if (power_on && CARD_REGISTERED) {
	do_release(VERY_FAST);
	if (toggles[T_AUTO_FEED].on) {
	    do_feed(false);
	}
    }
}

static void
Release_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    action_dbg("Release", wid, event, params, num_params);

    show_key_down(&rel_key);
    if (!power_on || !CARD_REGISTERED) {
	return;
    }
    rel_key_backend(&rel_key);
}

static void
Tab_action(Widget wid, XEvent *event, String *params, Cardinal *num_params)
{
    int i;

    action_dbg("Tab", wid, event, params, num_params);

    if (power_on && CARD_REGISTERED) {
	flush_typeahead();
	punch_state = C_FLUX;
	for (i = col; i < 6; i++) {
	    enq_event(KYBD_RIGHT, 1, false, SLOW);
	}
	enq_event(REGISTERED, 0, false, 0);
    }
}

/* Throw away this card. */
static void
drop_key_backend(kpkey_t *key)
{
    int i;

    if (!power_on || !CARD_REGISTERED) {
	return;
    }

    flush_typeahead();
    punch_state = C_FLUX;

    /* Do a Home operation. */
    for (i = 0; i <= col; i++) {
	enq_event(KEY_LEFT, 0, false, FAST);
    }

    /*
     * Scroll the print station card off to the right.
     * XXX: Another magic number.
     */
    for (i = 0; i < 87; i++) {
	enq_event(PAN_LEFT_PRINT, 0, false, FAST);
    }

    if (toggles[T_AUTO_FEED].on) {
	do_feed(true);
    } else {
	/* Queue up a state change. */
	enq_event(EMPTY, True, false, 0);
    }
}

/* Feed a new card. */
static void
feed_key_backend(kpkey_t *key)
{
    if (power_on && !eq_count && !CARD_REGISTERED) {
	do_feed(false);
    }
}

/* On-screen key support. */

/* Initialize a key. */
static void
key_init(kpkey_t *key, const char *name, Widget container, Position x,
	Position y, char *normal_pixmap_src[], char *pressed_pixmap_src[],
	key_backend_t backend)
{
    Pixmap shapemask;
    XpmAttributes attributes;

    memset(key, '\0', sizeof(*key));
    key->name = name;
    attributes.valuemask = XpmSize;
    if (XpmCreatePixmapFromData(display, XtWindow(container),
		normal_pixmap_src, &key->normal_pixmap, &shapemask,
		&attributes) != XpmSuccess) {
	XtError("XpmCreatePixmapFromData failed");
    }
    attributes.valuemask = XpmSize;
    if (XpmCreatePixmapFromData(display, XtWindow(container),
		pressed_pixmap_src, &key->pressed_pixmap, &shapemask,
		&attributes) != XpmSuccess) {
	XtError("XpmCreatePixmapFromData failed");
    }
    key->widget = XtVaCreateManagedWidget(
	    rel_key.name, commandWidgetClass, container,
	    XtNborderWidth, 0,
	    XtNlabel, "",
	    XtNbackgroundPixmap, key->normal_pixmap,
	    XtNheight, KEY_HEIGHT,
	    XtNwidth, KEY_WIDTH,
	    XtNx, x,
	    XtNy, y,
	    XtNhighlightThickness, 0,
	    NULL);
    XtAddCallback(key->widget, XtNcallback, key_press, (XtPointer)key);
    key->backend = backend;
}

/* Timeout callback. Pop the key back up. */
static void
pop_key(XtPointer data, XtIntervalId *id)
{
    kpkey_t *key = (kpkey_t *)data;

    XtVaSetValues(key->widget, XtNbackgroundPixmap, key->normal_pixmap, NULL);
    key->timeout_id = 0;
}

/* Handle graphical transitions for a click. */
static void
show_key_down(kpkey_t *key)
{
    XtVaSetValues(key->widget, XtNbackgroundPixmap, key->pressed_pixmap, NULL);
    if (key->timeout_id != 0) {
	XtRemoveTimeOut(key->timeout_id);
    }
    key->timeout_id = XtAppAddTimeOut(appcontext, VERY_SLOW, pop_key,
	    (XtPointer)key);
}

/*
 * Mouse click callback. Press the key down, schedule a timeout to pop it
 * back up, call the key-specific back end.
 */
static void
key_press(Widget w, XtPointer client_data, XtPointer call_data)
{
    kpkey_t *key = (kpkey_t *)client_data;

    dbg_printf("[callback] %s\n", key->name);
    show_key_down(key);
    if (key->backend) {
	key->backend(key);
    }
}

/*
 * Release the card in the punch station, i.e., scroll it off to the left.
 * This is just the release operation. Auto-feed needs to be implemented by the
 * caller.
 */
static void
do_release(int delay)
{
    int i;

    /* The card is now officially invisible. */
    flush_typeahead();
    punch_state = C_FLUX;

    /* Space over the remainder of the card. */
    for (i = col; i < N_COLS; i++) {
	enq_event(REL_RIGHT, 0, false, delay);
    }

    /*
     * Scroll the card out of the punch station.
     * XXX: The end column is a magic number.
     */
    for (i = 0; i < 22; i++) {
	enq_event(PAN_RIGHT_BOTH, 0, false, delay);
    }

    /* The punch station is now empty. */
    enq_event(EMPTY, false, false, 0);

    /* We've saved a new card. */
    enq_event(STACK, 0, false, 0);
}

/*
 * Release the card in the reader station, i.e., scroll it off to the left.
 * This is only used by the CLEAR switch.
 */
static void
do_clear_read(void)
{
    int i;

    /*
     * Scroll the card out of the read station.
     * XXX: The end column is a magic number.
     */
    for (i = 0; i < N_COLS + 14; i++) {
	enq_event(PAN_RIGHT_READ, 0, false, VERY_FAST);
    }

    /* We've saved a new card. */
    enq_event(STACK, 0, false, 0);
}

/* Pull a card from the (infinite) hopper into the punch station. */
static void
do_feed(bool keep_sequence)
{
    int i;

    enq_event(NEWCARD, keep_sequence, false, FAST);

    /* Scroll the new card down. */
    enq_event(SLAM, 0, false, SLOW);
    for (i = 0; i <= N_ROWS + 1; i++) {
	    enq_event(PAN_UP, 0, false, FAST);
    }
    for (i = SLAM_COL; i < SLAM_TARGET_COL; i++) {
	    enq_event(PAN_RIGHT_PRINT, 0, false, VERY_FAST);
    }

    /* There is now a card registered in the punch station. */
    enq_event(REGISTERED, 0, false, 0);
}

/* Start-up sequence. */
void
queued_POWER_ON(unsigned char ignored)
{
    power_on = true;
    XtVaSetValues(power_widget, XtNbackgroundPixmap, flipper_on, NULL);
}

void
queued_PRESS_FEED(unsigned char ignored)
{
    show_key_down(&feed_key);
}

static void
startup_power_feed(void)
{
    enq_event(POWER_ON, 0, false, VERY_SLOW);
    enq_event(PRESS_FEED, 0, false, VERY_SLOW);
    do_feed(false);
}

static void
startup_power(void)
{
    enq_event(POWER_ON, 0, false, VERY_SLOW);
}

void
queued_PRESS_REL(unsigned char ignored)
{
    show_key_down(&rel_key);
}

void
queued_EMPTY(unsigned char free_it)
{
    if (free_it && ps_card != NULL) {
	XtFree((XtPointer)ps_card);
	ps_card = NULL;
    }
    punch_state = C_EMPTY;
}

static void
display_card_count(void)
{
    char label[64];

    snprintf(label, sizeof(label), "-%04d-", card_count);
    XtVaSetValues(stacker, XtNlabel, label, NULL);
}

/*
 * Add a card to the stack, or throw it away.
 *
 * Updates the stacker count, which might be fake.
 */
static void
stack_card(card_t **c)
{
    if (mode == M_INTERACTIVE) {
	if (stack_last != NULL) {
	    stack_last->next = *c;
	} else {
	    stack = *c;
	}
	stack_last = *c;
    } else {
	XtFree((XtPointer)*c);
    }
    *c = NULL;
    card_count++;
    display_card_count();
}

/*
 * Shift the cards to the left.
 *
 * If we have a read station, shift the card from the punch station to the
 *  read station, and from the read station into the stacker.
 * Otherwise, shift the card from the punch station directly into the stacker.
 */
void
queued_STACK(unsigned char ignored)
{
    if (appres.read) {
	Widget w;

	if (rs_card != NULL) {
	    stack_card(&rs_card);
	}
	rs_card = ps_card;
	ps_card = NULL;

	/* Swap the windows. */
	w = ps_cardw;
	ps_cardw = rs_cardw;
	rs_cardw = w;
    } else {
	/* Move the card from the punch station into the stacker. */
	if (ps_card != NULL) {
	    stack_card(&ps_card);
	}
    }
}

/* Auto-play processing. */
typedef enum {
    DS_READ,	/* need to read from the file */
    DS_CHAR,	/* need to process a character from the file */
    DS_SPACE,	/* need to space over the rest of the card */
    DS_EOF	/* done */
} ap_state_t;
static const char *ds_name[] = { "READ", "CHAR", "SPACE", "EOF" };
XtInputId read_id = 0;
#define AP_BUFSIZE	1024
typedef struct {
    const char *name;	/* FSM name, for debug display */
    bool read;		/* true to fetch input, false to use static string */
    ap_state_t state;	/* state */
    char *buf;		/* input buffer */
    ssize_t rbsize;	/* buffer size */
    char *s;		/* buffer pointer */
} fsm_cx_t;
fsm_cx_t paste_fsm_cx, ap_fsm_cx;
static void run_fsm(fsm_cx_t *cx);

/* Input is now readable. */
static void
read_more(XtPointer closure, int *fd, XtInputId *id)
{
    XtRemoveInput(read_id);
    read_id = 0;
    run_fsm(&ap_fsm_cx);
}

static void
init_fsms()
{
    paste_fsm_cx.name = "paste";
    paste_fsm_cx.state = DS_READ;

    ap_fsm_cx.name = "ap";
    ap_fsm_cx.read = true;
    ap_fsm_cx.state = DS_READ;
    ap_fsm_cx.buf = XtMalloc(AP_BUFSIZE);
    ap_fsm_cx.rbsize = 0;
    ap_fsm_cx.s = ap_fsm_cx.buf + AP_BUFSIZE;
}

/* Run the paste and auto-play FSMs. */
void
run_fsms(void)
{
    if (eq_count == 0) {
	/* The paste FSM has absolute priority over the auto-play FSM. */
	if (paste_fsm_cx.state != DS_READ) {
	    run_fsm(&paste_fsm_cx);
	} else if (mode != M_INTERACTIVE) {
	    run_fsm(&ap_fsm_cx);
	}
    }
}

/* Clean up the state of the paste FSM. */
static void
paste_fsm_cleanup(fsm_cx_t *cx)
{
    if (!cx->read && cx->buf != NULL) {
	XtFree(cx->buf);
	cx->buf = NULL;
	cx->rbsize = 0;
	cx->s = NULL;
    }
}

/*
 * Crank a paste/auto-play FSM.
 */
static void
run_fsm(fsm_cx_t *cx)
{
    char c;

    do {
	dbg_printf("[%s fsm] %s\n", cx->name, ds_name[cx->state]);

	switch (cx->state) {

	case DS_READ:
	    if (!cx->read) {
		return;
	    }

	    /* Keep munching on the same buffer. */
	    if (cx->s < cx->buf + cx->rbsize) {
		dbg_printf("[%s fsm]  continuing, %zd more\n", cx->name, cx->buf + cx->rbsize - cx->s);
	    } else {
		ssize_t nr;

		/* Read the next card. */
		nr = read(ap_fd, cx->buf, AP_BUFSIZE);
		dbg_printf("[%s fsm]  got %zd chars\n", cx->name, nr);
		if (nr == 0) {
		    if (read_id != 0) {
			XtRemoveInput(read_id);
			read_id = 0;
		    }
		    close(ap_fd);
		    ap_fd = -1;

		    /* Next, exit. */
		    cx->state = DS_EOF;
		    break;
		}
		if (nr < 0) {
		    if (errno == EWOULDBLOCK) {
			if (read_id == 0)
			    read_id = XtAppAddInput(appcontext, ap_fd,
				    (XtPointer)XtInputReadMask, read_more,
				    NULL);
			return;
		    } else {
			perror("read(stdin)");
			exit(1);
		    }
		}
		cx->rbsize = nr;
		cx->s = cx->buf;
	    }

	    /* Next, start munching on it. */
	    cx->state = DS_CHAR;
	    break;

	case DS_CHAR:
	    if (!CARD_REGISTERED) {
		/* XXX: It might be in flux? */
		static bool unfed = true;

		if (mode == M_BATCH && unfed) {
		    unfed = false;
		    show_key_down(&feed_key);
		}
		do_feed(false);
		if (mode == M_BATCH) {
		    enq_delay();
		}
		break;
	    }
	    c = *(cx->s);
	    cx->s++;
	    dbg_printf("[%s fsm]  c = 0x%02x, col = %d\n", cx->name, c & 0xff, col);
	    if (c == '\n') {
		/*
		 * End of input line.
		 *
		 * Delay if there is anything to see; start
		 * spacing to the end of the card.
		 */
		if (col) {
		    enq_event(PRESS_REL, 0, false, VERY_SLOW);
		} else {
		    enq_event(PRESS_REL, 0, false, 0);
		}
		cx->state = DS_SPACE;
		break;
	    }
	    did_auto_rel = false;
	    add_char(c);
	    if (cx->s >= cx->buf + cx->rbsize) {
		/*
		 * Ran out of buffer without a newline.
		 *
		 * Go read some more.
		 */
		cx->state = DS_READ;
		paste_fsm_cleanup(cx);
		continue;
	    }
	    break;

	case DS_SPACE:
	    if (!did_auto_rel) {
		do_release(FAST);
		/*
		 * In remote control mode, create a new card.
		 * In auto-play mode, wait for data before
		 * doing it.
		 */
		if (mode == M_REMOTECTL || !cx->read) {
		    do_feed(false);
		}
	    }
	    if (cx->s >= cx->buf + cx->rbsize) {
		cx->state = DS_READ;
		paste_fsm_cleanup(cx);
	    } else {
		cx->state = DS_CHAR;
	    }
	    break;

	case DS_EOF:
	    /* Done. */
	    auto_feed_off();
	    enq_event(CLEAR_SEQ, 0, false, SLOW * 3);
	    break;
	}
    } while (eq_count == 0 && power_on);
}

/* Add a pasted character. */
void
add_paste_char(unsigned char c)
{
    fsm_cx_t *cx = &paste_fsm_cx;

    if (cx->buf == NULL) {
	/* First char. */
	cx->buf = XtMalloc(1);
	cx->rbsize = 0;
	cx->state = DS_CHAR;
    } else {
	/* Subsequent char. */
	size_t left = cx->rbsize - (cx->s - cx->buf);
	char *buf = XtMalloc(left + 1);

	memcpy(buf, cx->s, left);
	XtFree(cx->buf);
	cx->buf = buf;
	cx->rbsize = left;
    }
    cx->buf[cx->rbsize++] = c;
    cx->s = cx->buf;
}

/* Make sure the paste operation runs. */
void
poke_fsm(void)
{
    enq_event(DUMMY, 0, false, 0);
}

/*
 * The shutdown sequence for auto-play.
 */
void
queued_CLEAR_SEQ(unsigned char ignored)
{
    /*
     * Hit the clear switch, which will enqueue operations to scroll the
     * card off of the read station.
     */
    clear_switch();

    /* Enqueue the OFF operation after that. */
    enq_event(OFF, 0, false, SLOW * 3);

    /*
     * Enqueue a long dummy operation, to give the OFF time to run and to
     * keep the auto-play FSM from being called again.
     */
    enq_event(DUMMY, 0, false, 6 * 1000);
}

/* Accessor functions for appres. */

Pixel
get_errcolor(void)
{
    return appres.errcolor;
}

Pixel
get_cabinet(void)
{
    return appres.cabinet;
}

Pixel
get_foreground(void)
{
    return appres.foreground;
}

charset_t
get_charset(void)
{
    return ccharset;
}

/* Debug printing. */
void
dbg_printf(const char *format, ...)
{
    va_list ap;
    struct timeval tv;

    if (!appres.debug) {
	return;
    }

    gettimeofday(&tv, NULL);
    printf("%lu.%06lu ", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);

    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
    fflush(stdout);
}

void
dbg_cprintf(const char *format, ...)
{
    va_list ap;

    if (!appres.debug) {
	return;
    }

    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
    fflush(stdout);
}

void
action_dbg(const char *name, Widget wid, XEvent *event, String *params,
	Cardinal *num_params)
{
    Cardinal i;

    if (!appres.debug) {
	return;
    }

    dbg_printf("[action] %s(", name);
    for (i = 0; i < *num_params; i++) {
	dbg_cprintf("%s%s", i? ", ": "", params[i]);
    }
    dbg_cprintf(") widget %p", (void *)wid);
    if (event) {
	if (event->type == Expose) {
	    dbg_cprintf(" Expose x=%d y=%d w=%d h=%d",
		    event->xexpose.x,
		    event->xexpose.y,
		    event->xexpose.width,
		    event->xexpose.height);
	} else {
	    dbg_cprintf(" event %d", event->type);
	}
    }
    dbg_cprintf("\n");
}

void
set_next_card_image(cardimg_t c)
{
    ncardimg = c;
}

int
set_charset(charset_t c)
{
    ccharset = c;
    if (ps_card != NULL) {
	ps_card->charset = c;
    }
    return 0;
}

bool
debugging(void)
{
    return appres.debug;
}
