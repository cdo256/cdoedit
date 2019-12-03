/* See LICENSE file for copyright and license details. */

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *font = "Liberation Mono:pixelsize=12:antialias=true:autohint=true";
static int borderpx = 2;

/* identification sequence returned in DA and DECID */
char *vtiden = "\033[?6c";

/* Kerning / character bounding-box multipliers */
static float cwscale = 1.0;
static float chscale = 1.0;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
wchar_t *worddelimiters = L" ";

/* alt screens */
int allowaltscreen = 1;

/* frames per second cdoedit should at maximum draw to the screen */
static unsigned int xfps = 120;
static unsigned int actionfps = 30;

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
static unsigned int blinktimeout = 800;

/*
 * thickness of underline and bar cursors
 */
static unsigned int cursorthickness = 2;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
static int bellvolume = 0;

/*
 * spaces per tab
 */
unsigned int tabspaces = 8;

/*
 * Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
unsigned int defaultfg = 1;
unsigned int defaultbg = 0;
static unsigned int defaultcs = 1;
static unsigned int defaultrcs = 0;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static unsigned int cursorshape = 2;

/*
 * Default columns and rows numbers
 */

static unsigned int cols = 80;
static unsigned int rows = 24;

/*
 * Default colour and shape of the mouse cursor
 */
static unsigned int mouseshape = XC_xterm;

/*
 * Color used to display font attributes when fontconfig selected a font which
 * doesn't match the ones requested.
 */
static unsigned int defaultattr = 1;

/* Internal keyboard shortcuts. */
#define META Mod1Mask
#define CTRL ControlMask
#define SHIFT ShiftMask
#define NOSHIFT 0

static Shortcut shortcuts[] = {
	/* editing */
	/* mask                 keysym          function        argument */
	{ NOSHIFT,              XK_Tab,         changeindent,   {.i = +1} },
	{ SHIFT,                XK_Tab,         changeindent,   {.i = -1} },
	{ CTRL,                 XK_Home,        zoomreset,      {.f =  0} },
	{ CTRL,                 XK_C,           clipcopy,       {.i =  0} },
	{ CTRL,                 XK_V,           clippaste,      {.i =  0} },
	{ CTRL,                 XK_Y,           selpaste,       {.i =  0} },
	{ CTRL,                 XK_Num_Lock,    numlock,        {.i =  0} },
	{ 0,                    XK_Return,      newline,        {.i =  0} },
	{ 0,                    XK_BackSpace,   deletechar,     {.i = -1} },
	{ 0,                    XK_Delete,      deletechar,     {.i = +1} },
	{ CTRL,                 XK_BackSpace,   deleteword,     {.i = -1} },
	{ CTRL,                 XK_Delete,      deleteword,     {.i = +1} },

	/* navigation */
	/* mask                 keysym          function        argument */
	{ NOSHIFT,              XK_Home,        navline,        {.i = -1} },
	{ SHIFT,                XK_Home,        navline,        {.i = -2} },
	{ NOSHIFT,              XK_End,         navline,        {.i = +1} },
	{ SHIFT,                XK_End,         navline,        {.i = +2} },
	{ CTRL|NOSHIFT,         XK_Home,        navdocument,    {.i = -1} },
	{ CTRL|SHIFT,           XK_Home,        navdocument,    {.i = -2} },
	{ CTRL|SHIFT,           XK_End,         navdocument,    {.i = +1} },
	{ CTRL|NOSHIFT,         XK_End,         navdocument,    {.i = +2} },
	{ NOSHIFT,              XK_Up,          navrow,         {.i = -1} },
	{ SHIFT,                XK_Up,          navrow,         {.i = -2} },
	{ NOSHIFT,              XK_Down,        navrow,         {.i = +1} },
	{ SHIFT,                XK_Down,        navrow,         {.i = +2} },
	{ NOSHIFT,              XK_Left,        navchar,        {.i = -1} },
	{ SHIFT,                XK_Left,        navchar,        {.i = -2} },
	{ NOSHIFT,              XK_Right,       navchar,        {.i = +1} },
	{ SHIFT,                XK_Right,       navchar,        {.i = +2} },
	{ CTRL|NOSHIFT,         XK_Up,          navparagraph,   {.i = -1} },
	{ CTRL|SHIFT,           XK_Up,          navparagraph,   {.i = -2} },
	{ CTRL|NOSHIFT,         XK_Down,        navparagraph,   {.i = +1} },
	{ CTRL|SHIFT,           XK_Down,        navparagraph,   {.i = +2} },
	{ CTRL|NOSHIFT,         XK_Left,        navword,        {.i = -1} },
	{ CTRL|SHIFT,           XK_Left,        navword,        {.i = -2} },
	{ CTRL|NOSHIFT,         XK_Right,       navword,        {.i = +1} },
	{ CTRL|SHIFT,           XK_Right,       navword,        {.i = +2} },
	{ NOSHIFT,              XK_Page_Up,     navpage,        {.i = -1} },
	{ SHIFT,                XK_Page_Up,     navpage,        {.i = -2} },
	{ NOSHIFT,              XK_Page_Down,   navpage,        {.i = +1} },
	{ SHIFT,                XK_Page_Down,   navpage,        {.i = +2} },

	/* other */
	/* mask                 keysym          function        argument */
	{ CTRL,                 XK_plus,        zoom,           {.f = +1} },
	{ CTRL,                 XK_minus,       zoom,           {.f = -1} },
};

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (Mod2Mask) and keyboard layout (XK_SWITCH_MOD) are ignored.
 */
static uint ignoremod = Mod2Mask|XK_SWITCH_MOD;

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
static char ascii_printable[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

