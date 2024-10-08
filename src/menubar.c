/*--------------------------------*-C-*---------------------------------*
 * File:	menubar.c
 *----------------------------------------------------------------------*
 * Copyright (C) 1997,1998  mj olesen <olesen@me.QueensU.CA>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *----------------------------------------------------------------------*
 * refer.html (or refer.txt) contains up-to-date documentation.  The
 * summary that appears at the end of this file was taken from there.
 *----------------------------------------------------------------------*/

#ifndef lint
/* JWT:DEPRECIATED? - JUST CAUSES WARNINGS!: static const char rcsid[] = "$Id: menubar.c,v 1.1.1.1 2004/11/10 17:21:46 sasha Exp $"; */
#endif

#include "rxvt.h"		/* NECESSARY */

/* #define DEBUG_MENU */
/* #define DEBUG_MENU_LAYOUT */
/* #define DEBUG_MENUBAR_STACKING */

#define HSPACE		1	/* one space */
#define MENU_MARGIN	2
#define menu_height()	(TermWin.fheight + 2 * MENU_MARGIN)

#define MENU_DELAY_USEC	250000	/* 1/4 sec */

#define SEPARATOR_HALFHEIGHT	(SHADOW + 1)
#define SEPARATOR_HEIGHT	(2 * SEPARATOR_HALFHEIGHT)
#define isSeparator(name)	((name)[0] == '\0')

#define SEPARATOR_NAME		"-"
#define MENUITEM_BEG		'{'
#define MENUITEM_END		'}'
#define COMMENT_CHAR		'#'

#define DOT	"."
#define DOTS	".."

#ifdef MENUBAR
#define Menu_PixelWidth(menu)					\
    (2 * SHADOW +  Width2Pixel ((menu)->width + 3 * HSPACE))

static GC       topShadowGC, botShadowGC, neutralGC, menubarGC;

struct menu_t;

static int      menu_readonly = 1;	/* okay to alter menu? */
static Bool     popupvisible; /* TRUE MEANS (NON-MENUBAR) POPUP MENU IS DISPLAYED */
/* static char     prev_aname[80]; */ /* JWT:BUFFER FOR LAST ACTIVE-MENU NAME (EFFICIENCY) - MENU NAMES SHOULD *NEVER* EXCEED 79 CHARS! */
static menuitem_t  *prev_menu; /* JWT:BUFFER FOR LAST ACTIVE-MENU NAME (EFFICIENCY) - MENU NAMES SHOULD *NEVER* EXCEED 79 CHARS! */
static Cursor   menu_cursor; /* JWT:GIVE OUR POPUP MENU THE SAME CURSOR USED BY MENUBAR MENUS */

#ifndef NO_MENUBAR_ARROWS
static int      Arrows_x = 0;

static const struct {
    char            name;	/* (l)eft, (u)p, (d)own, (r)ight */
    unsigned char   str[4];	/* str[0] = strlen (str+1) */
} Arrows[NARROWS] = {
    { 'l', "\003\033[D" },
    { 'u', "\003\033[A" },
    { 'd', "\003\033[B" },
    { 'r', "\003\033[C" }
};
#endif

#if (MENUBAR_MAX > 1)
static int      Nbars = 0;
static bar_t   *CurrentBar = NULL;
#else	
static bar_t    BarList;
static bar_t   *CurrentBar = &BarList;
#endif

static menu_t  *ActiveMenu = NULL;	/* currently active menu */
#endif

/*}}} */

/*
 * find an item called NAME in MENU
 */
/* PROTO */
menuitem_t     *
menuitem_find(menu_t * menu, char *name)
{
#ifdef MENUBAR
    menuitem_t     *item;

    assert(name != NULL);
    assert(menu != NULL);

/* find the last item in the menu, this is good for separators */
    for (item = menu->tail; item != NULL; item = item->prev) {
	if (item->entry.type == MenuSubMenu) {
	    if (!strcmp(name, (item->entry.submenu.menu)->name))
		break;
	} else if ((isSeparator(name) && isSeparator(item->name)) ||
		   !strcmp(name, item->name))
	    break;
    }
    return item;
#else
    return NULL;
#endif
}

/*
 * unlink ITEM from its MENU and free its memory
 */
/* PROTO */
void
menuitem_free(menu_t * menu, menuitem_t * item)
{
#ifdef MENUBAR
/* disconnect */
    menuitem_t     *prev, *next;

    assert(menu != NULL);

    prev = item->prev;
    next = item->next;
    if (prev != NULL)
	prev->next = next;
    if (next != NULL)
	next->prev = prev;

/* new head, tail */
    if (menu->tail == item)
	menu->tail = prev;
    if (menu->head == item)
	menu->head = next;

    switch (item->entry.type) {
    case MenuAction:
    case MenuTerminalAction:
	FREE(item->entry.action.str);
	break;
    case MenuSubMenu:
    if (menu != CurrentBar->popup)  /* JWT:AVOID DOUBLE-DELETE! */
		(void)menu_delete(item->entry.submenu.menu);
	break;
    }
    if (item->name != NULL)
	FREE(item->name);
    if (item->name2 != NULL)
	FREE(item->name2);
    FREE(item);
#endif
}

/* JWT:ADDED 20240801 TO SUPPORT HEX CHARS BY CONVERTING 0x## TO CORRESPONDING CHARACTER: */
int hex2char (unsigned char * str, int len)
{
	short i, j;
	j = 0;
	i = 0;
	char buf[5];
	while (i < len && j < 80) {
		if (str[i] == '0' && i+3 < len && str[i+1] == 'x') {
			str[i+2] = (str[i+2]<58) ? str[i+2] - 48 : str[i+2] - 87;
			str[i+3] = (str[i+3]<58) ? str[i+3] - 48 : str[i+3] - 87;
			sprintf(buf, "%d", str[i+2]*16 + str[i+3]);
			str[j++] = atoi(buf);
			i += 4;
		} else {
			str[j++] = str[i++];
		}
	}
	str[j] = '\0';
	return j;
}

/*
 * sort command vs. terminal actions and
 * remove the first character of STR if it's '\0'
 */
/* PROTO */
int
action_type(action_t * action, unsigned char *str)
{
#ifdef MENUBAR
    unsigned int    len;

#if defined (DEBUG_MENU) || defined (DEBUG_MENUARROWS)
    len = strlen(str);
    printf("(len %d) = %s\n", len, str);
#else
    len = Str_escaped((char *) str);
#endif
	len = hex2char(str, len);  /* JWT:ADDED TO CONVERT HEX CHARS IN MENUITEM-ACTION STRINGS TO ACTUAL CHARS. */

    if (!len)
	return -1;

/* sort command vs. terminal actions */
    action->type = MenuAction;
    if (str[0] == '\0') {
    /* the functional equivalent: memmove (str, str+1, len); */
	unsigned char  *dst = (str);
	unsigned char  *src = (str + 1);
	unsigned char  *end = (str + len);

	while (src <= end)
	    *dst++ = *src++;

	len--;			/* decrement length */
	if (str[0] != '\0')
	    action->type = MenuTerminalAction;
    }
    action->str = str;
    action->len = len;
#endif

    return 0;
}

/* PROTO */
int
action_dispatch(action_t * action)
{
#ifdef MENUBAR
    switch (action->type) {
    case MenuTerminalAction:
	cmd_write(action->str, action->len);
	break;

    case MenuAction:
	tt_write(action->str, action->len);
	break;

    default:
	return -1;
	break;
    }
#endif
    return 0;
}

/* JWT:NEW FUNCTION TO SEND CMD. DIRECTLY TO TERMINAL W/O AN ACTION OBJECT: */
void
command_dispatch(char * esc_sequence)
{
#ifdef MENUBAR
	unsigned int len;
	unsigned char str[50];
	menu_reallyhide_all(NULL);
	strcpy((char *)str, esc_sequence);
	len = Str_escaped((char *) str);
	len = hex2char(str, len);  /* JWT:ADDED TO CONVERT HEX CHARS IN MENUITEM-ACTION STRINGS TO ACTUAL CHARS. */
	cmd_write(str, len);
#endif
}

/* return the arrow index corresponding to NAME */
/* PROTO */
int
menuarrow_find(char name)
{
#ifdef MENUBAR
#ifndef NO_MENUBAR_ARROWS
    int             i;

    for (i = 0; i < NARROWS; i++)
	if (name == Arrows[i].name)
	    return i;
#endif
#endif
    return -1;
}

/* free the memory associated with arrow NAME of the current menubar */
/* PROTO */
void
menuarrow_free(char name)
{
#ifdef MENUBAR
#ifndef NO_MENUBAR_ARROWS
    int             i;

    if (name) {
	i = menuarrow_find(name);
	if (i >= 0) {
	    action_t       *act = &(CurrentBar->arrows[i]);

	    switch (act->type) {
	    case MenuAction:
	    case MenuTerminalAction:
		FREE(act->str);
		act->str = NULL;
		act->len = 0;
		break;
	    }
	    act->type = MenuLabel;
	}
    } else {
	for (i = 0; i < NARROWS; i++)
	    menuarrow_free(Arrows[i].name);
    }
#endif
#endif
}

/* PROTO */
void
menuarrow_add(char *string)
{
#ifdef MENUBAR
#ifndef NO_MENUBAR_ARROWS
    int             i;
    unsigned        xtra_len;
    char           *p;

    struct {
	char           *str;
	int             len;
    } beg = {
	NULL, 0
    }, end = {
	NULL, 0
    }, *cur, parse[NARROWS];

    MEMSET(parse, 0, sizeof(parse));

/* printf ("add arrows = `%s'\n", string); */
    for (p = string; p != NULL && *p; string = p) {
	p = (string + 3);
    /* printf ("parsing at %s\n", string); */
	switch (string[1]) {
	case 'b':
	    cur = &beg;
	    break;
	case 'e':
	    cur = &end;
	    break;

	default:
	    i = menuarrow_find(string[1]);
	    if (i >= 0)
		cur = &(parse[i]);
	    else
		continue;	/* not found */
	    break;
	}

	string = p;
	cur->str = string;
	cur->len = 0;

	if (cur == &end) {
	    p = strchr(string, '\0');
	} else {
	    char           *next = string;

	    while (1) {
		p = strchr(next, '<');
		if (p != NULL) {
		    if (p[1] && p[2] == '>')
			break;
		/* parsed */
		} else {
		    if (beg.str == NULL)	/* no end needed */
			p = strchr(next, '\0');
		    break;
		}
		next = (p + 1);
	    }
	}

	if (p == NULL)
	    return;
	cur->len = (p - string);
    }

#ifdef DEBUG_MENUARROWS
    cur = &beg;
    printf("<b>(len %d) = %.*s\n",
	   cur->len, cur->len, (cur->str ? cur->str : ""));
    for (i = 0; i < NARROWS; i++) {
	cur = &(parse[i]);
#ifndef NO_MENUBAR_ARROWS
	printf("<%c>(len %d) = %.*s\n",
	       Arrows[i].name,
	       cur->len,
	       cur->len,
	       (cur->str ? cur->str : ""));
#endif
    }
    cur = &end;
    printf("<e>(len %d) = %.*s\n",
	   cur->len, cur->len, (cur->str ? cur->str : ""));
#endif

    xtra_len = (beg.len + end.len);
    for (i = 0; i < NARROWS; i++) {
	if (xtra_len || parse[i].len)
	    menuarrow_free(Arrows[i].name);
    }

    for (i = 0; i < NARROWS; i++) {
	unsigned char  *str;
	unsigned int    len;

	if (!parse[i].len)
	    continue;

	str = MALLOC(parse[i].len + xtra_len + 1);
	if (str == NULL)
	    continue;

	len = 0;
	if (beg.len) {
	    STRNCPY(str + len, beg.str, beg.len);
	    len += beg.len;
	}
	STRNCPY(str + len, parse[i].str, parse[i].len);
	len += parse[i].len;

	if (end.len) {
	    STRNCPY(str + len, end.str, end.len);
	    len += end.len;
	}
	str[len] = '\0';

#ifdef DEBUG_MENUARROWS
	printf("<%c>(len %d) = %s\n", Arrows[i].name, len, str);
#endif
	if (action_type(&(CurrentBar->arrows[i]), str) < 0)
	    FREE(str);
    }
#endif
#endif
}

/* PROTO */
menuitem_t     *
menuitem_add(menu_t * menu, char *name, char *name2, char *action)
{
#ifdef MENUBAR
    menuitem_t     *item;
    unsigned int    len;

    assert(name != NULL);
    assert(action != NULL);

    if (menu == NULL)
	return NULL;

    if (isSeparator(name)) {
    /* add separator, no action */
	name = "";
	action = "";
    } else {
    /*
     * add/replace existing menu item
     */
	item = menuitem_find(menu, name);
	if (item != NULL) {
	    if (item->name2 != NULL && name2 != NULL) {
		FREE(item->name2);
		item->len2 = 0;
		item->name2 = NULL;
	    }
	    switch (item->entry.type) {
	    case MenuAction:
	    case MenuTerminalAction:
		FREE(item->entry.action.str);
		item->entry.action.str = NULL;
		break;
	    }
	    goto Item_Found;
	}
    }
/* allocate a new itemect */
    if ((item = (menuitem_t *)MALLOC(sizeof(menuitem_t))) == NULL)
	return NULL;

    item->len2 = 0;
    item->name2 = NULL;

    len = strlen(name);
    item->name = MALLOC(len + 1);
    if (item->name != NULL) {
	STRCPY(item->name, name);
	if (name[0] == '.' && name[1] != '.')
	    len = 0;		/* hidden menu name */
    } else {
	FREE(item);
	return NULL;
    }
    item->len = len;

/* add to tail of list */
    item->prev = menu->tail;
    item->next = NULL;

    if (menu->tail != NULL)
	(menu->tail)->next = item;
    menu->tail = item;
/* fix head */
    if (menu->head == NULL)
	menu->head = item;

/*
 * add action
 */
  Item_Found:
    if (name2 != NULL && item->name2 == NULL) {
	len = strlen(name2);
	if (len == 0 || (item->name2 = MALLOC(len + 1)) == NULL) {
	    len = 0;
	    item->name2 = NULL;
	} else {
	    STRCPY(item->name2, name2);
	}
	item->len2 = len;
    }
    item->entry.type = MenuLabel;
    len = strlen(action);

    if (len == 0 && item->name2 != NULL) {
	action = item->name2;
	len = item->len2;
    }
    if (len) {
	unsigned char  *str = MALLOC(len + 1);

	if (str == NULL) {
	    menuitem_free(menu, item);
	    return NULL;
	}
	STRCPY(str, action);

	if (action_type(&(item->entry.action), str) < 0)
	    FREE(str);
    }
/* new item and a possible increase in width */
    if (menu->width < (item->len + item->len2))
	menu->width = (item->len + item->len2);

    return item;
#else
    return NULL;
#endif
}

/*
 * search for the base starting menu for NAME.
 * return a pointer to the portion of NAME that remains
 */
/* PROTO */
char           *
menu_find_base(menu_t ** menu, char *path)
{
#ifdef MENUBAR
    menu_t         *m = NULL;
    menuitem_t     *item;

    assert(menu != NULL);
    assert(CurrentBar != NULL);

    if (path[0] == '\0')
	return path;

    if (strchr(path, '/') != NULL) {
	register char  *p = path;

	while ((p = strchr(p, '/')) != NULL) {
	    p++;
	    if (*p == '/')
		path = p;
	}
	if (path[0] == '/') {
	    path++;
	    *menu = NULL;
	}
	while ((p = strchr(path, '/')) != NULL) {
	    p[0] = '\0';
	    if (path[0] == '\0')
		return NULL;
	    if (!strcmp(path, DOT)) {
	    /* nothing to do */
	    } else if (!strcmp(path, DOTS)) {
		if (*menu != NULL)
		    *menu = (*menu)->parent;
	    } else {
		path = menu_find_base(menu, path);
		if (path[0] != '\0') {	/* not found */
		    p[0] = '/';	/* fix-up name again */
		    return path;
		}
	    }

	    path = (p + 1);
	}
    }
    if (!strcmp(path, DOTS)) {
	path += strlen(DOTS);
	if (*menu != NULL)
	    *menu = (*menu)->parent;
	return path;
    }
/* find this menu */
    if (*menu == NULL) {
	for (m = CurrentBar->tail; m != NULL; m = m->prev) {
	    if (!strcmp(path, m->name))
		break;
	}
    } else {
    /* find this menu */
	for (item = (*menu)->tail; item != NULL; item = item->prev) {
	    if (item->entry.type == MenuSubMenu &&
		!strcmp(path, (item->entry.submenu.menu)->name)) {
		m = (item->entry.submenu.menu);
		break;
	    }
	}
    }
    if (m != NULL) {
	*menu = m;
	path += strlen(path);
    }
    return path;
#else
    return NULL;
#endif
}

/*
 * delete this entire menu
 */
/* PROTO */
menu_t         *
menu_delete(menu_t * menu)
{
#ifdef MENUBAR
    menu_t         *parent = NULL, *prev, *next;
    menuitem_t     *item;

    assert(CurrentBar != NULL);

/* delete the entire menu */
    if (menu == NULL)
	return NULL;

    parent = menu->parent;

/* unlink MENU */
    prev = menu->prev;
    next = menu->next;
    if (prev != NULL)
	prev->next = next;
    if (next != NULL)
	next->prev = prev;

/* fix the index */
    if (parent == NULL) {
	const int       len = (menu->len + HSPACE);

	/* JWT:PARENTLESS MENUS ARE HANGING AS MENUITEMS ON OUR POPUP MENU TOO, SO REMOVE IT THERE IF SO: */

	if (CurrentBar->tail == menu)
	    CurrentBar->tail = prev;
	if (CurrentBar->head == menu)
	    CurrentBar->head = next;

	for (next = menu->next; next != NULL; next = next->next)
	    next->x -= len;
    } else {
	for (item = parent->tail; item != NULL; item = item->prev) {
	    if (item->entry.type == MenuSubMenu &&
		item->entry.submenu.menu == menu) {
		item->entry.submenu.menu = NULL;
		menuitem_free(menu->parent, item);
		break;
	    }
	}
    }

    item = menu->tail;
    while (item != NULL) {
	menuitem_t     *p = item->prev;

	menuitem_free(menu, item);
	item = p;
    }

    if (menu->name != NULL)
	FREE(menu->name);
    FREE(menu);

    return parent;
#else
    return NULL;
#endif
}

/* PROTO */
menu_t         *
menu_add(menu_t * parent, char *path, Bool rjust, Bool hiddn)
{
#ifdef MENUBAR
    menu_t         *menu;

    assert(CurrentBar != NULL);

    if (strchr(path, '/') != NULL) {
	register char  *p;

	if (path[0] == '/') {
	/* shouldn't happen */
	    path++;
	    parent = NULL;
	}
	while ((p = strchr(path, '/')) != NULL) {
	    p[0] = '\0';
	    if (path[0] == '\0')
		return NULL;

	    parent = menu_add(parent, path, False, False);
	    path = (p + 1);
	}
    }
    if (!strcmp(path, DOTS))
	return (parent != NULL ? parent->parent : parent);

    if (!strcmp(path, DOT) || path[0] == '\0')
	return parent;

/* allocate a new menu */
    if ((menu = (menu_t *)MALLOC(sizeof(menu_t))) == NULL)
	return parent;

    menu->width = 0;
    menu->parent = parent;
    menu->len = strlen(path);
    menu->name = MALLOC((menu->len + 1));
    if (menu->name == NULL) {
	FREE(menu);
	return parent;
    }
    STRCPY(menu->name, path);

/* initialize head/tail */
    menu->head = menu->tail = NULL;
    menu->prev = menu->next = NULL;

    menu->win = None;
    menu->x = menu->y = menu->w = menu->h = 0;
    menu->right_just = rjust;
    menu->hidden = hiddn;
    menu->displayed = False;
    menu->item = NULL;

/* add to tail of list */
    if (parent == NULL) {
		menu->prev = CurrentBar->tail;
		if (CurrentBar->tail != NULL)
		    CurrentBar->tail->next = menu;
		CurrentBar->tail = menu;
		if (CurrentBar->head == NULL)
		    CurrentBar->head = menu;	/* fix head */
		if (menu->prev)
	        menu->x = (menu->prev->x + menu->prev->len + HSPACE);

		/* JWT:NEXT 2 IF-BLOCKS ADDED 20140814 TO DO POPUP VERSION OF THE MENUBAR MENUS: */
		if (CurrentBar->popup == NULL) {
		    if ((CurrentBar->popup = (menu_t *)MALLOC(sizeof(menu_t))) != NULL) {
	            CurrentBar->popup->width = 0;
	            CurrentBar->popup->parent = NULL;
	            CurrentBar->popup->name = NULL;
	            CurrentBar->popup->len = 0;  /* JWT:MUST STAY ZERO, AS menu_show() USES THIS TO TELL WE'RE A NON-MENUBAR MENU! */
	            CurrentBar->popup->head = NULL;
	            CurrentBar->popup->tail = NULL;
	            CurrentBar->popup->prev = NULL;
	            CurrentBar->popup->next = NULL;
	            CurrentBar->popup->item = NULL;
	            CurrentBar->popup->win = None;
	            CurrentBar->popup->hidden = False;
	            CurrentBar->popup->displayed = False;
	        }
		}
	    if (CurrentBar->popup != NULL) { /* JWT:WHEN ADDED, ADD EACH MENUBAR-BUTTON MENU TO OUR POPUP MENU: */
	        menuitem_t *item = menuitem_add(CurrentBar->popup, menu->name, "", "");
			if (item != NULL) {
				assert(item->entry.type == MenuLabel);
				item->name = menu->name;
				item->len = menu->len;
				item->entry.type = MenuSubMenu;
				item->entry.submenu.menu = menu;
			}
		}
    } else {
		menuitem_t     *item;

		item = menuitem_add(parent, path, "", "");
		if (item == NULL) {
		    FREE(menu);
		    return parent;
		}
		assert(item->entry.type == MenuLabel);
		item->entry.type = MenuSubMenu;
		item->entry.submenu.menu = menu;
    }

    return menu;
#else
    return NULL;
#endif
}

/* PROTO */
void
drawbox_menubar(int x, int len, int state, int shift_pix)
{
#ifdef MENUBAR
    GC              top, bot;

    x = Width2Pixel(x);
	if (x > 0 || len < TermWin.ncol) /* JWT:UNLESS FILLING ENTIRE MENUBAR ROW: */
        x += shift_pix;

    len = Width2Pixel(len + HSPACE);
    if (x > TermWin.width) {
        len -= (x - TermWin.width);
        x = TermWin.width;
	}
    if (x + shift_pix + len >= TermWin.width)
        len = (TermWin_TotalWidth() - (x-shift_pix));

#ifdef MENUBAR_SHADOW_IN
    state = -state;
#endif
    switch (state) {
    case +1:
	top = topShadowGC;
	bot = botShadowGC;
	break;			/* SHADOW_OUT */
    case -1:
	top = botShadowGC;
	bot = topShadowGC;
	break;			/* SHADOW_IN */
    default:
	top = bot = neutralGC;
	break;			/* neutral */
    }

    Draw_Shadow(menuBar.win, top, bot,
		x, 0, len, menuBar_TotalHeight());
#endif
}

/* PROTO */
void
drawtriangle(int x, int y, int state, char direction)
{
#ifdef MENUBAR
    GC              top, bot;
    int             w;

#ifdef MENU_SHADOW_IN
    state = -state;
#endif
    switch (state) {
    case +1:
	top = topShadowGC;
	bot = botShadowGC;
	break;			/* SHADOW_OUT */
    case -1:
	top = botShadowGC;
	bot = topShadowGC;
	break;			/* SHADOW_IN */
    default:
	top = bot = neutralGC;
	break;			/* neutral */
    }

    w = menu_height() / 2;

    if (x >= 0)
        x -= (SHADOW + MENU_MARGIN) + (3 * w / 2);
    else
        x = -1 * x;  /* JWT:NEED ABILITY TO CALL THIS W/O ADJUSTMENTS NOW (IN MENUBAR). */

    y += (SHADOW + MENU_MARGIN) + (w / 2);

    Draw_Triangle(ActiveMenu->win, top, bot, x, y, w, direction);
#endif
}

/* PROTO */
void
drawbox_menuitem(int y, int state)
{
#ifdef MENUBAR
    GC              top, bot;

#ifdef MENU_SHADOW_IN
    state = -state;
#endif
    switch (state) {
    case +1:
	top = topShadowGC;
	bot = botShadowGC;
	break;			/* SHADOW_OUT */
    case -1:
	top = botShadowGC;
	bot = topShadowGC;
	break;			/* SHADOW_IN */
    default:
	top = bot = neutralGC;
	break;			/* neutral */
    }

    Draw_Shadow(ActiveMenu->win, top, bot,
		SHADOW + 0,
		SHADOW + y,
		ActiveMenu->w - 2 * (SHADOW),
		menu_height() + 2 * MENU_MARGIN);
    XFlush(Xdisplay);
#endif
}

#ifdef DEBUG_MENU_LAYOUT
/* PROTO */
void
print_menu_ancestors(menu_t * menu)
{
#ifdef MENUBAR
    if (menu == NULL) {
	printf("Top Level menu\n");
	return;
    }
    printf("menu %s ", menu->name);
    if (menu->parent != NULL) {
	menuitem_t     *item;

	for (item = menu->parent->head; item != NULL; item = item->next) {
	    if (item->entry.type == MenuSubMenu &&
		item->entry.submenu.menu == menu)
			break;
	}
	if (item == NULL) {
	    printf("is an orphan!\n");
	    return;
	}
    }
    printf("\n");
    print_menu_ancestors(menu->parent);
#endif
}

/* PROTO */
void
print_menu_descendants(menu_t * menu)
{
#ifdef MENUBAR
    menuitem_t     *item;
    menu_t         *parent;
    int             i, level = 0;

    parent = menu;
    do {
	level++;
	parent = parent->parent;
    }
    while (parent != NULL);

    for (i = 0; i < level; i++)
	printf(">");
    printf("%s\n", menu->name);

    for (item = menu->head; item != NULL; item = item->next) {
	if (item->entry.type == MenuSubMenu) {
	    if (item->entry.submenu.menu == NULL)
		printf("> %s == NULL\n", item->name);
	    else
		print_menu_descendants(item->entry.submenu.menu);
	} else {
	    for (i = 0; i < level; i++)
		printf("+");
	    if (item->entry.type == MenuLabel)
		printf("label: ");
	    printf("%s\n", item->name);
	}
    }

    for (i = 0; i < level; i++)
	printf("<");
    printf("\n");
#endif
}
#endif

/* pop up/down the current menu and redraw the menuBar button */
/* PROTO */
void
menu_show(void)
{
#ifdef MENUBAR
	int             x, y, xright;
	int             rs_pix = 0;
	menuitem_t     *item;

	if (ActiveMenu == NULL)
		return;  /* noting to show. */

	x = popupvisible ? CurrentBar->popup->x + CurrentBar->popup->w : ActiveMenu->x;
	y = popupvisible ? CurrentBar->popup->y : ActiveMenu->y;
	if (ActiveMenu->parent == NULL) {
		register int    h;
		if (!popupvisible) {
			rs_pix = (ActiveMenu->right_just && scrollbar_visible()) ? (2 * sb_shadow)+SB_WIDTH : 0;
			drawbox_menubar(x, ActiveMenu->len, -1, rs_pix);
	 		if (ActiveMenu->name != NULL && ActiveMenu->name[0] == '<' && ActiveMenu->name[1] == 0)
	 			return;  /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */

			x = Width2Pixel(x);

			/* JWT:CHGD. TO NEXT TO FIX OFF-BY-1 BUG NOTICABLE ON TRANSPARENT TERMINALS!:    ActiveMenu->y = 1; */
			if (ActiveMenu->len > 0)
				ActiveMenu->y = 0;

			if ((Options & Opt_scrollBar_right) || x < rs_pix)
					x += rs_pix; /* JWT:WE'RE NOW IN TERMWIN COORDS, SO ONLY SHIFT DD-MENU IF SB ON RIGHT */
					/* -OR- IF (SCROLLBAR ON LEFT AND) MENUBUTTON IS ABOVE SCROLLBAR (LEFT OF TEXT): */
		}
		ActiveMenu->w = Menu_PixelWidth(ActiveMenu);
		if ((x + ActiveMenu->w) >= TermWin.width)
			x = (TermWin_TotalWidth() - ActiveMenu->w);
		if (ActiveMenu->len > 0) {
			/* find the height */
			for (h = 0, item = ActiveMenu->head; item != NULL; item = item->next) {
				if (isSeparator(item->name))
					h += SEPARATOR_HEIGHT;
				else
					h += menu_height();
			}
			ActiveMenu->h = h + 2 * (SHADOW + MENU_MARGIN);
		}
	} else if (popupvisible)
		/* JWT:REPLACE NEXT LINE W/THIS TO OPEN SUB-SUB MENUS HALF OVER PARENT: x += (ActiveMenu->parent->w / 2); */
		/* x += ActiveMenu->parent->w; */
		x += (ActiveMenu->parent->w / 2);

	if (ActiveMenu->win == None)
		ActiveMenu->win = XCreateSimpleWindow(Xdisplay, TermWin.vt,
				x,
				y,
				ActiveMenu->w,
				ActiveMenu->h,
				0,
				PixColors[Color_fg],
				PixColors[Color_scroll]);
				XDefineCursor(Xdisplay, ActiveMenu->win, menu_cursor);
				XMapWindow(Xdisplay, ActiveMenu->win);

	Draw_Shadow(ActiveMenu->win,
			topShadowGC, botShadowGC,
			0, 0,
	ActiveMenu->w, ActiveMenu->h);

	/* determine the correct right-alignment */
	for (xright = 0, item = ActiveMenu->head; item != NULL; item = item->next) {
		if (item->len2 > xright)
			xright = item->len2;
	}

	const int       xoff = (SHADOW + Width2Pixel(HSPACE) / 2);
	const int       yoff = (SHADOW + MENU_MARGIN);
	for (y = 0, item = ActiveMenu->head;
			item != NULL;
			item = item->next) 
	{
		register int    h;
		GC              gc = menubarGC;

		if (isSeparator(item->name)) {
			Draw_Shadow(ActiveMenu->win,
					topShadowGC, botShadowGC,
					xoff,
					yoff + y + SEPARATOR_HALFHEIGHT,
					ActiveMenu->w - (2 * xoff),
			0);
			h = SEPARATOR_HEIGHT;
		} else {
			char           *name = item->name;
			int             len = item->len;

			if (item->entry.type == MenuLabel) {
				/*xxx gc = botShadowGC; */
			} else if (item->entry.type == MenuSubMenu) {
				int             x1, y1;
				menuitem_t     *it;
				menu_t         *menu = item->entry.submenu.menu;

				if (menu->hidden)  /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */
					continue;

				if (menu->name[0] != '<')  /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */
					drawtriangle(ActiveMenu->w, y, +1, 'r'); /* right-arrow on menu items w/submenus */

				name = menu->name;
				len = menu->len;

				/* place sub-menu at midpoint of parent menu */
				menu->w = Menu_PixelWidth(menu);
				if (ActiveMenu->len > 0) {
					x1 = ActiveMenu->w / 2;
					y1 = ActiveMenu->y + y;
					} else
				x1 = y1 = 0;

				/* right-flush menu if it's too small */
				if (x1 > menu->w)
					x1 += (x1 - menu->w);

				x1 += x;

				/* find the height of this submenu */
				for (h = 0, it = menu->head; it != NULL; it = it->next) {
					if (isSeparator(it->name))
							h += SEPARATOR_HEIGHT;
					else
					h += menu_height();
				}
				menu->h = h + 2 * (SHADOW + MENU_MARGIN);

				/* ensure menu is in window limits */
						if ((x1 + menu->w) >= TermWin.width)
				x1 = (TermWin_TotalWidth() - menu->w);

				if ((y1 + menu->h) >= TermWin.height)
				y1 = (TermWin_TotalHeight() - menu->h);

				if (ActiveMenu->len > 0) {
					menu->x = (x1 < 0 ? 0 : x1);
					menu->y = (y1 < 0 ? 0 : y1);
				}
			} else if (item->name2 && !strcmp(name, item->name2))
				name = NULL;

			if (len && name) {
				if (len == 1 && name[0] == '<') {  /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */
					drawtriangle(-1*xoff, 0, +1, 'l'); /* left-arrow on our special button. */
					if (popupvisible)
						XDrawString(Xdisplay,
							ActiveMenu->win, gc,
							xoff+20,
							yoff + y + menu_height() - (2 * MENU_MARGIN),
							"Prev.", 5);
				} else
					XDrawString(Xdisplay,
						ActiveMenu->win, gc,
						xoff,
						yoff + y + menu_height() - (2 * MENU_MARGIN),
						name, len);
			}

			len = item->len2;
			name = item->name2;
			if (len && name)
					XDrawString(Xdisplay,
					ActiveMenu->win, gc,
					ActiveMenu->w - (xoff + Width2Pixel(xright)),
					yoff + y + menu_height() - (2 * MENU_MARGIN),
			name, len);

			h = menu_height();
		}
		y += h;
	}
	ActiveMenu->displayed = True;
#endif
}

/* JWT:NEW FUNCTION TO POPUP/DOWN MENUBAR MENUS AS SINGLE POPUP MENU VIA Control+MouseButton-3: */
/* PROTO */
void
menubar_menu_show(Bool popit, int x, int y)
{
#ifdef MENUBAR
	if (CurrentBar == NULL || CurrentBar->popup == NULL
			|| CurrentBar->popup->head == NULL || menubarGC == None)
		return;  /* PUNT, NO MENUS OR NOT SET UP! */

	prev_menu = NULL;
	if (!popit) {
		menu_reallyhide_all(NULL);
		return;
	}

    menuitem_t     *item;
    int h = 0;
	for (item = CurrentBar->popup->head; item != NULL; item = item->next) {
		if (item->entry.type == MenuSubMenu && item->entry.submenu.menu->hidden)
			continue;    /* JWT:DON'T DRAW SPECIAL "<"(Previous Menubar) BUTTON WHEN HIDDEN: */

		h += menu_height();
	}
	h += 2 * (SHADOW + MENU_MARGIN);
	CurrentBar->popup->x = x;
	if (y + h > Width2Pixel(TermWin.nrow*2))
		y = Width2Pixel(TermWin.nrow*2) - h;

	CurrentBar->popup->y = y;
	CurrentBar->popup->w = 0;
	CurrentBar->popup->h = h;
	CurrentBar->popup->len = 0;
	CurrentBar->popup->right_just = False;
	ActiveMenu = CurrentBar->popup;
	popupvisible = True;
	menu_show();
#endif
}

/* PROTO */
void
menu_display(void (*update) (void))
{
#ifdef MENUBAR
    int rs_pix;
    menu_t *am;

    if (ActiveMenu == NULL)
        return;

    if (ActiveMenu->win != None)
		XDestroyWindow(Xdisplay, ActiveMenu->win);

    ActiveMenu->win = None;
    ActiveMenu->item = NULL;
    rs_pix = (ActiveMenu->right_just && scrollbar_visible()) ? (2 * sb_shadow)+SB_WIDTH : 0;

    if (!ActiveMenu->hidden && ActiveMenu->parent == NULL && ActiveMenu->len > 0)
        drawbox_menubar(ActiveMenu->x, ActiveMenu->len, +1, rs_pix);

    am = ActiveMenu;
    ActiveMenu = ActiveMenu->parent;
    update();
    am->displayed = False;
#endif
}

/* PROTO */
void
menu_hide_all(void)
{
#ifdef MENUBAR
    menu_display(menu_hide_all);
#endif
}

/* PROTO */
void
menu_hide(void)
{
#ifdef MENUBAR
    menu_display(menu_show);
#endif
}

/* JWT:NEW FUNCTION ADDED TO REALLY HIDE ALL THE MULTIPLE POPUP MENUBARS (POPUP MENUS): */
/* PROTO */
void
menu_reallyhide_all(menu_t * topmenu)
{
#ifdef MENUBAR
	static int recursion_level = 0;

	if (recursion_level == 0 && topmenu == NULL) {
		topmenu = (CurrentBar == NULL) ? ActiveMenu : CurrentBar->popup;
		popupvisible = False;
	}
	if (topmenu == NULL)
		return;

	menuitem_t *item;

	recursion_level += 1;
	for (item = topmenu->head; item != NULL; item = item->next) {
		if (item->entry.type == MenuSubMenu && item->entry.submenu.menu != NULL
				&& !item->entry.submenu.menu->hidden)
			menu_reallyhide_all(item->entry.submenu.menu);
	}
	recursion_level -= 1;

	ActiveMenu = topmenu;
	if (topmenu->displayed) {
		menu_hide();
	}
	if (recursion_level == 0 && topmenu == CurrentBar->popup)
		prev_menu = NULL;
#endif
}

/* PROTO */
void
menu_clear(menu_t * menu)
{
#ifdef MENUBAR
    if (menu != NULL) {
	menuitem_t     *item = menu->tail;

	while (item != NULL) {
	    menuitem_free(menu, item);
	/* it didn't get freed ... why? */
	    if (item == menu->tail)
		return;
	    item = menu->tail;
	}
	menu->width = 0;
    }
#endif
}

/* JWT:NEW FUNCTION TO TELL command.c WHETHER POPUP MENUS ARE CURRENTLY DISPLAYED: */
/* PROTO */
Bool
popupmenu_visible(void)
{
#ifdef MENUBAR
	return popupvisible;
#else
	return False;
#endif
}

/* PROTO */
void
menubar_clear(void)
{
#ifdef MENUBAR
	popupvisible = False;
    if (CurrentBar != NULL) {
	menu_t         *menu = CurrentBar->tail;

	while (menu != NULL) {
	    menu_t         *prev = menu->prev;

	    menu_delete(menu);
	    menu = prev;
	}
	CurrentBar->head = CurrentBar->tail = ActiveMenu = NULL;

	if (CurrentBar->title) {
	    FREE(CurrentBar->title);
	    CurrentBar->title = NULL;
	}
	menuarrow_free(0);	/* remove all arrow functions */
    }
    ActiveMenu = NULL;
#endif
}

#if (MENUBAR_MAX > 1)
/* find if menu already exists */
/* PROTO */
bar_t          *
menubar_find(const char *name)
{
#ifdef MENUBAR
    bar_t          *bar = CurrentBar;

#ifdef DEBUG_MENUBAR_STACKING
    printf("looking for [menu:%s] ...", name ? name : "(nil)");
#endif
    if (bar == NULL || name == NULL)
	return NULL;

    if (strlen(name) && strcmp(name, "*")) {
	do {
	    if (!strcmp(bar->name, name)) {
#ifdef DEBUG_MENUBAR_STACKING
		printf(" found!\n");
#endif
		return bar;
	    }
	    bar = bar->next;
	}
	while (bar != CurrentBar);
	bar = NULL;
    }
#ifdef DEBUG_MENUBAR_STACKING
    printf("%s found!\n", (bar ? "" : " NOT"));
#endif

    return bar;
#else
    return NULL;
#endif
}

/* PROTO */
int
menubar_push(const char *name)
{
    int             ret = 1;
#ifdef MENUBAR
    bar_t          *bar;

    if (CurrentBar == NULL) {
    /* allocate first one */
	bar = (bar_t *)MALLOC(sizeof(bar_t));

	if (bar == NULL)
	    return 0;

    MEMSET(bar, 0, sizeof (bar_t));
    /* circular linked-list */
	bar->next = bar->prev = bar;
	bar->head = bar->tail = NULL;
	bar->popup = NULL;
	bar->title = NULL;
	CurrentBar = bar;
	Nbars++;

	menubar_clear();
    } else {
    /* find if menu already exists */
	bar = menubar_find(name);
	if (bar != NULL)
	    CurrentBar = bar;  /* found it, use it */
	else {
	/* create if needed, or reuse the existing empty menubar */
	    if (CurrentBar->head != NULL) {
	    /* need to malloc another one */
		if (Nbars < MENUBAR_MAX)
		    bar = (bar_t *)MALLOC(sizeof(bar_t));
		else
		    bar = NULL;

	    /* malloc failed or too many menubars, reuse another */
		if (bar == NULL) {
		    bar = CurrentBar->next;
		    ret = -1;
		} else {
		    bar->head = bar->tail = NULL;
		    bar->popup = NULL;
		    bar->title = NULL;

		    bar->next = CurrentBar->next;
		    CurrentBar->next = bar;
		    bar->prev = CurrentBar;
		    bar->next->prev = bar;

		    Nbars++;
		}
		CurrentBar = bar;

	    }
	    menubar_clear();
	}
    }

/* give menubar this name */
    STRNCPY(CurrentBar->name, name, MAXNAME);
    CurrentBar->name[MAXNAME - 1] = '\0';
#endif

    return ret;
}

/* switch to a menu called NAME and remove it */
/* PROTO */
void
menubar_remove(const char *name)
{
#ifdef MENUBAR
    bar_t          *bar;

    if ((bar = menubar_find(name)) == NULL)
	return;
    CurrentBar = bar;

    do {
	menubar_clear();
    /*
     * pop a menubar, clean it up first
     */
	if (CurrentBar != NULL) {
	    bar_t          *prev = CurrentBar->prev;
	    bar_t          *next = CurrentBar->next;

	    if (prev == next && prev == CurrentBar) {	/* only 1 left */
		prev = NULL;
		Nbars = 0;	/* safety */
	    } else {
		next->prev = prev;
		prev->next = next;
		Nbars--;
	    }

	    FREE(CurrentBar);
	    CurrentBar = prev;
	}
    }
    while (CurrentBar && !strcmp(name, "*"));

    if (Nbars == 1 && CurrentBar != NULL && CurrentBar->head != NULL) {
    	   /* JWT:ONLY 1 MENUBAR LEFT, HIDE THE SPECIAL "Prev. Menubar" ("<") BUTTON!: */
    	   char *found;
    	   menu_t *prevhidden = CurrentBar->head;
    	   found = menu_find_base(&prevhidden, "<");
    	   if (found[0])
			prevhidden->hidden = True;
    }
#endif
}

/* PROTO */
void
action_decode(FILE * fp, action_t * act)
{
#ifdef MENUBAR
    unsigned char  *str;
    short           len;

    if (act == NULL || (len = act->len) == 0 || (str = act->str) == NULL)
	return;

    if (act->type == MenuTerminalAction) {
	fprintf(fp, "^@");
    /* can strip trailing ^G from XTerm sequence */
	if (str[0] == 033 && str[1] == ']' && str[len - 1] == 007)
	    len--;
    } else if (str[0] == 033) {
	switch (str[1]) {
	case '[':
	case ']':
	    break;

	case 'x':
	/* can strip trailing '\r' from M-x sequence */
	    if (str[len - 1] == '\r')
		len--;
	/* drop */

	default:
	    fprintf(fp, "M-");	/* meta prefix */
	    str++;
	    len--;
	    break;
	}
    }
/*
 * control character form is preferred, since backslash-escaping
 * can be really ugly looking when the backslashes themselves also
 * have to be escaped to avoid Shell (or whatever scripting
 * language) interpretation
 */
    while (len > 0) {
	unsigned char   ch = *str++;

	switch (ch) {
	case 033:
	    fprintf(fp, "\\E");
	    break;		/* escape */
	case '\r':
	    fprintf(fp, "\\r");
	    break;		/* carriage-return */
	case '\\':
	    fprintf(fp, "\\\\");
	    break;		/* backslash */
	case '^':
	    fprintf(fp, "\\^");
	    break;		/* caret */
	case 127:
	    fprintf(fp, "^?");
	default:
	    if (ch <= 31)
		fprintf(fp, "^%c", ('@' + ch));
	    else if (ch > 127)
		fprintf(fp, "\\%o", ch);
	    else
		fprintf(fp, "%c", ch);
	    break;
	}
	len--;
    }
    fprintf(fp, "\n");
#endif
}

/* PROTO */
void
menu_dump(FILE * fp, menu_t * menu)
{
#ifdef MENUBAR
    menuitem_t     *item;

/* create a new menu and clear it */
    fprintf(fp, (menu->parent ? "./%s/*\n" : "/%s/*\n"), menu->name);

    for (item = menu->head; item != NULL; item = item->next) {
	switch (item->entry.type) {
	case MenuSubMenu:
	    if (item->entry.submenu.menu == NULL)
		fprintf(fp, "> %s == NULL\n", item->name);
	    else
		menu_dump(fp, item->entry.submenu.menu);
	    break;

	case MenuLabel:
	    fprintf(fp, "{%s}\n",
		    (strlen(item->name) ? item->name : "-"));
	    break;

	case MenuTerminalAction:
	case MenuAction:
	    fprintf(fp, "{%s}", item->name);
	    if (item->name2 != NULL && strlen(item->name2))
		fprintf(fp, "{%s}", item->name2);
	    fprintf(fp, "\t");
	    action_decode(fp, &(item->entry.action));
	    break;
	}
    }

    fprintf(fp, (menu->parent ? "../\n" : "/\n\n"));
#endif
}

/* PROTO */
void
menubar_dump(FILE * fp)
{
#ifdef MENUBAR
    bar_t          *bar = CurrentBar;
    time_t          t;

    if (bar == NULL || fp == NULL)
	return;
    time(&t);

    fprintf(fp,
	    "# " APL_SUBCLASS " (%s)  Pid: %u\n# Date: %s\n\n",
	    rs_name, (unsigned int)getpid(), ctime(&t));

/* dump in reverse order */
    bar = CurrentBar->prev;
    do {
	menu_t         *menu;

	fprintf(fp, "[menu:%s]\n", bar->name);

	if (bar->title != NULL)
	    fprintf(fp, "[title:%s]\n", bar->title);

#ifndef NO_MENUBAR_ARROWS
	for (int i = 0; i < NARROWS; i++) {
	    switch (bar->arrows[i].type) {
	    case MenuTerminalAction:
	    case MenuAction:
		fprintf(fp, "<%c>", Arrows[i].name);
		action_decode(fp, &(bar->arrows[i]));
		break;
	    }
	}
#endif
	fprintf(fp, "\n");

	for (menu = bar->head; menu != NULL; menu = menu->next)
	    menu_dump(fp, menu);

	fprintf(fp, "\n[done:%s]\n\n", bar->name);
	bar = bar->prev;
    }
    while (bar != CurrentBar->prev);
#endif
}
#endif				/* (MENUBAR_MAX > 1) */

/*
 * read in menubar commands from FILENAME
 * ignore all input before the tag line [menu] or [menu:???]
 *
 * Note that since File_find () is used, FILENAME can be semi-colon
 * delimited such that the second part can refer to a tag
 * so that a large `database' of menus can be collected together
 *
 * FILENAME = "file"
 * FILENAME = "file;"
 *      read `file' starting with first [menu] or [menu:???] line
 *
 * FILENAME = "file;tag"
 *      read `file' starting with [menu:tag]
 */
/* PROTO */
void
menubar_read(const char *filename)
{
#ifdef MENUBAR
/* read in a menu from a file */
    FILE           *fp;
    char            buffer[256];
    char           *p, *tag = NULL;
    const char     *file;

    file = File_find(filename, ".menu");
    if (file == NULL || (fp = fopen(file, "rb")) == NULL)
        return;

#if (MENUBAR_MAX > 1)
/* semi-colon delimited */
    if ((tag = strchr(filename, ';')) != NULL) {
	tag++;
	if (*tag == '\0')
	    tag = NULL;
    }
#endif				/* (MENUBAR_MAX > 1) */
#ifdef DEBUG_MENU
    printf("[read:%s]\n", p);
    if (tag)
	printf("looking for [menu:%s]\n", tag);
#endif

    while ((p = fgets(buffer, sizeof(buffer), fp)) != NULL) {
	int             n;

	if ((n = Str_match(p, "[menu")) != 0) {
	    if (tag) {
	    /* looking for [menu:tag] */
		if (p[n] == ':' && p[n + 1] != ']') {
		    n++;
		    n += Str_match(p + n, tag);
		    if (p[n] == ']') {
#ifdef DEBUG_MENU
			printf("[menu:%s]\n", tag);
#endif
			break;
		    }
		}
	    } else if (p[n] == ':' || p[n] == ']')
		break;
	}
    }

/* found [menu], [menu:???] tag */
    while (p != NULL) {
	int             n;

#ifdef DEBUG_MENU
	printf("read line = %s\n", p);
#endif

    /* looking for [done:tag] or [done:] */
	if ((n = Str_match(p, "[done")) != 0) {
	    if (p[n] == ']') {
		menu_readonly = 1;
		break;
	    } else if (p[n] == ':') {
		n++;
		if (p[n] == ']') {
		    menu_readonly = 1;
		    break;
		} else if (tag) {
		    n += Str_match(p + n, tag);
		    if (p[n] == ']') {
#ifdef DEBUG_MENU
			printf("[done:%s]\n", tag);
#endif
			menu_readonly = 1;
			break;
		    }
		} else {
		/* what? ... skip this line */
		    p[0] = COMMENT_CHAR;
		}
	    }
	}
    /*
     * remove leading/trailing space
     * and strip-off leading/trailing quotes
     * skip blank or comment lines
     */
	p = Str_trim(p);
	if (p != NULL && *p && *p != '#') {
	    menu_readonly = 0;	/* if case we read another file */
	    menubar_dispatch(p);
	}
    /* get another line */
	p = fgets(buffer, sizeof(buffer), fp);
    }

    fclose(fp);
#endif
}

/*
 * user interface for building/deleting and otherwise managing menus
 */
/* PROTO */
void
menubar_dispatch(char *str)
{
#ifdef MENUBAR
    static menu_t  *BuildMenu = NULL;	/* the menu currently being built */
    int             n, cmd;
    char           *path, *name, *name2;
    Bool right_just = False;

    if (menubar_visible() && ActiveMenu != NULL)
	menubar_expose();
    else
	ActiveMenu = NULL;

    cmd = *str;
    switch (cmd) {
	case '>':
/* ADD MENU TO RIGHT SIDE OF MENUBAR! */
	right_just = True;
    cmd = '/';
    case '.':
    case '/':			/* absolute & relative path */
    case MENUITEM_BEG:		/* menuitem */
    /* add `+' prefix for these cases */
	cmd = '+';
	break;

    case '+':
    case '-':
	str++;			/* skip cmd character */
	break;

    case '<':
#if (MENUBAR_MAX > 1)
	if (CurrentBar == NULL)
	    break;
#endif				/* (MENUBAR_MAX > 1) */
	if (str[1] && str[2] == '>')	/* arrow commands */
	    menuarrow_add(str);
	break;

    case '[':			/* extended command */
	while (str[0] == '[') {
	    char           *next = (++str);	/* skip leading '[' */

	    if (str[0] == ':') {	/* [:command:] */
		do {
		    next++;
		    if ((next = strchr(next, ':')) == NULL)
			return;	/* parse error */
		}
		while (next[1] != ']');
	    /* remove and skip ':]' */
		*next = '\0';
		next += 2;
	    } else {
		if ((next = strchr(next, ']')) == NULL)
		    return;	/* parse error */
	    /* remove and skip ']' */
		*next = '\0';
		next++;
	    }

	    if (str[0] == ':') {
		int             saved;

	    /* try and dispatch it, regardless of read/write status */
		saved = menu_readonly;
		menu_readonly = 0;
		menubar_dispatch(str + 1);
		menu_readonly = saved;
	    }
	/* these ones don't require menu stacking */
	    else if (!strcmp(str, "clear")) {
		menubar_clear();
	    } else if (!strcmp(str, "done") || Str_match(str, "done:")) {
		menu_readonly = 1;
	    } else if (!strcmp(str, "show")) {
		map_menuBar(1);
		menu_readonly = 1;
	    } else if (!strcmp(str, "hide")) {
		map_menuBar(0);
		menu_readonly = 1;
	    } else if ((n = Str_match(str, "read:")) != 0) {
	    /* read in a menu from a file */
		str += n;
		menubar_read(str);
	    } else if ((n = Str_match(str, "title:")) != 0) {
		str += n;
		if (CurrentBar != NULL && !menu_readonly) {
		    if (*str) {
			name = REALLOC(CurrentBar->title, strlen(str) + 1);
			if (name != NULL) {
			    STRCPY(name, str);
			    CurrentBar->title = name;
			}
			menubar_expose();
		    } else {
			FREE(CurrentBar->title);
			CurrentBar->title = NULL;
		    }
		}
	    } else if ((n = Str_match(str, "pixmap:")) != 0) {
		str += n;
		xterm_seq(XTerm_Pixmap, str);
	    }
#if (MENUBAR_MAX > 1)
	    else if ((n = Str_match(str, "rm")) != 0) {
		str += n;
		switch (str[0]) {
		case ':':
		    str++;
		    menubar_remove(str);
		    break;

		case '\0':
		    menubar_remove(str);
		    break;

		case '*':
		    menubar_remove(str);
		    break;
		}
		menu_readonly = 1;
	    } else if ((n = Str_match(str, "menu")) != 0) {
		str += n;
		switch (str[0]) {
		case ':':
		    str++;
		/* add/access menuBar */
		    if (*str != '\0' && *str != '*')
		        menubar_push(str);
		    break;
                default:
                    if (CurrentBar == NULL) {
                        menubar_push("default");
                    }
		}
                    

		if (CurrentBar != NULL)
		    menu_readonly = 0;	/* allow menu build commands */
	    } else if (!strcmp(str, "dump")) {
	    /* dump current menubars to a file */
		FILE           *fp;

	    /* enough space to hold the results */
		char            buffer[32];

		sprintf(buffer, "/tmp/" APL_SUBCLASS "-%u",
			(unsigned int)getpid());

		if ((fp = fopen(buffer, "wb")) != NULL) {
		    xterm_seq(XTerm_title, buffer);
		    menubar_dump(fp);
		    fclose(fp);
		}
	    } else if (!strcmp(str, "next")) {
		if (CurrentBar) {
		    CurrentBar = CurrentBar->next;
		    menu_readonly = 1;
		}
	    } else if (!strcmp(str, "prev")) {
		if (CurrentBar) {
		    CurrentBar = CurrentBar->prev;
		    menu_readonly = 1;
		}
	    } else if (!strcmp(str, "swap")) {
	    /* swap the top 2 menus */
		if (CurrentBar) {
		    bar_t          *prev = CurrentBar->prev;
		    bar_t          *next = CurrentBar->next;

		    prev->next = next;
		    next->prev = prev;

		    CurrentBar->next = prev;
		    CurrentBar->prev = prev->prev;

		    prev->prev->next = CurrentBar;
		    prev->prev = CurrentBar;

		    CurrentBar = prev;
		    menu_readonly = 1;
		}
	    }
#endif				/* (MENUBAR_MAX > 1) */
	    str = next;

	    BuildMenu = ActiveMenu = NULL;
	    menubar_expose();
#ifdef DEBUG_MENUBAR_STACKING
	    printf("menus are read%s\n", menu_readonly ? "only" : "/write");
#endif
	}
	return;
	break;
    }  /* end big switch. */

#if (MENUBAR_MAX > 1)
    if (CurrentBar == NULL)
        return;

    if (menu_readonly) {
#ifdef DEBUG_MENUBAR_STACKING
	printf("menus are read%s, PUNT!\n", menu_readonly ? "only" : "/write");
#endif
	return;
    }
#endif				/* (MENUBAR_MAX > 1) */

    switch (cmd) {
    case '+':
    case '-':
	path = name = str;

	name2 = NULL;
    /* parse STR, allow spaces inside (name)  */
	if (path[0] != '\0') {
	    name = strchr(path, MENUITEM_BEG);
	    str = strchr(path, MENUITEM_END);
	    if (name != NULL || str != NULL) {
		if (name == NULL || str == NULL || str <= (name + 1)
		    || (name > path && name[-1] != '/')) {
		    print_error("menu error <%s>\n", path);
		    break;
		}
		if (str[1] == MENUITEM_BEG) {
		    name2 = (str + 2);
		    str = strchr(name2, MENUITEM_END);

		    if (str == NULL) {
			print_error("menu error <%s>\n", path);
			break;
		    }
		    name2[-2] = '\0';	/* remove prev MENUITEM_END */
		}
		if (name > path && name[-1] == '/')
		    name[-1] = '\0';

		*name++ = '\0';	/* delimit */
		*str++ = '\0';	/* delimit */

		while (isspace(*str))
		    str++;	/* skip space */
	    }
#ifdef DEBUG_MENU
	    printf("`%c' path = <%s>, name = <%s>, name2 = <%s>, action = <%s>\n",
		   cmd,
		   (path ? path : "(nil)"),
		   (name ? name : "(nil)"),
		   (name2 ? name2 : "(nil)"),
		   (str ? str : "(nil)")
		);
#endif
	}
    /* process the different commands */
	switch (cmd) {
	case '+':		/* add/replace existing menu or menuitem */
	    if (path[0] != '\0') {
		int             len;

		if (path[0] == '>')
			path++;  /* strip off right-justification symbol. */

		path = menu_find_base(&BuildMenu, path);
		len = strlen(path);

	    /* don't allow menus called `*' */
		if (path[0] == '*') {
		    menu_clear(BuildMenu);
		    break;
		} else if (len >= 2 && !strcmp((path + len - 2), "/*"))
		    path[len - 2] = '\0';

		if (path[0] != '\0') {
		    if (CurrentBar->popup == NULL) {
			    /* JWT:ADD SPECIAL "PREV. MENUBAR" BUTTON AS 1ST (LEFTMOST) BUTTON ON BAR: */
			    if (CurrentBar == CurrentBar->prev)
			        BuildMenu = menu_add(NULL, "<", False, True); /* JWT:IF 1ST MENUBAR, HIDE IT! */
			    else {  /* JWT:ON SUBSEQUENT MENUBAR ADDED, ADD IT, BUT UNHIDE THE ONE ON THE PREVIOUS BAR!: */
			    	   char *found;
			    	   menu_t *prevhidden = CurrentBar->prev->head;
			    	   BuildMenu = menu_add(NULL, "<", False, False);
			    	   found = menu_find_base(&prevhidden, "<");
			    	   if (found[0])
			    	       prevhidden->hidden = False;
			    }
			    /*??? ACTIONLESS? menuitem_add(BuildMenu, "Prev MenuBar", "", "aterm_menus.sh [prev]\\r"); */
			    BuildMenu = NULL;
			}
		    BuildMenu = menu_add(BuildMenu, path, right_just, False);
		    right_just = False;
		}
	    }
	    if (name != NULL && name[0] != '\0') {
		if (!strcmp(name, SEPARATOR_NAME))
		    name = "";
		menuitem_add(BuildMenu, name, name2, str);
	    }
	    break;

	case '-':		/* delete menu entry */
	    if (!strcmp(path, "/*") && (name == NULL || name[0] == '\0')) {
		menubar_clear();
		BuildMenu = NULL;
		menubar_expose();
		break;
	    } else if (path[0] != '\0') {
		int             len;
		menu_t         *menu = BuildMenu;

		path = menu_find_base(&menu, path);
		len = strlen(path);

	    /* submenu called `*' clears all menu items */
		if (path[0] == '*') {
		    menu_clear(menu);
		    break;	/* done */
		} else if (len >= 2 && !strcmp(&path[len - 2], "/*")) {
		/* done */
		    break;
		} else if (path[0] != '\0') {
		    BuildMenu = NULL;
		    break;
		} else {
		    BuildMenu = menu;
		}
	    }
	    if (BuildMenu != NULL) {
		if (name == NULL || name[0] == '\0') {
		    BuildMenu = menu_delete(BuildMenu);
		} else {
		    menuitem_t     *item;

		    if (!strcmp(name, SEPARATOR_NAME))
			name = "";
		    item = menuitem_find(BuildMenu, name);

		    if (item != NULL && item->entry.type != MenuSubMenu) {
			menuitem_free(BuildMenu, item);

		    /* fix up the width */
			BuildMenu->width = 0;
			for (item = BuildMenu->head;
			     item != NULL;
			     item = item->next) {
			    if (BuildMenu->width < (item->len + item->len2))
				BuildMenu->width = (item->len + item->len2);
			}
		    }
		}
		menubar_expose();
	    }
	    break;
	}
	break;
    }
#endif
}

/* PROTO */
void
draw_Arrows(int name, int state)
{
#ifdef MENUBAR
#ifndef NO_MENUBAR_ARROWS
    GC              top, bot;

    int             i;

#ifdef MENU_SHADOW_IN
    state = -state;
#endif
    switch (state) {
    case +1:
	top = topShadowGC;
	bot = botShadowGC;
	break;			/* SHADOW_OUT */
    case -1:
	top = botShadowGC;
	bot = topShadowGC;
	break;			/* SHADOW_IN */
    default:
	top = bot = neutralGC;
	break;			/* neutral */
    }

    if (!Arrows_x)
	return;

    for (i = 0; i < NARROWS; i++) {
        const int       w = Width2Pixel(1);
        const int       y = (menuBar_TotalHeight() - w) / 2;
        int             x = Arrows_x + (5 * Width2Pixel(i)) / 4;

        if (!name || name == Arrows[i].name)
            Draw_Triangle(menuBar.win, top, bot, x, y, w, Arrows[i].name);
    }
    XFlush(Xdisplay);
#endif
#endif
}

/* PROTO */
void
menubar_init(void)
{
#ifdef MENUBAR
    if (menubarGC == None) {
    /* Create the graphics context */
	XGCValues       gcvalue;

	gcvalue.font = TermWin.font->fid;

	gcvalue.foreground = (Xdepth <= 2 ?
			      PixColors[Color_fg] :
/* JWT:CHGD. TO NEXT (CAN'T SEE WHAT WE'RE DOIN')!:			      PixColors[Color_Black]); */
			      PixColors[Color_widgetFG]);
	menubarGC = XCreateGC(Xdisplay, menuBar.win,
			      GCForeground | GCFont,
			      &gcvalue);

	gcvalue.foreground = PixColors[Color_scroll];
	neutralGC = XCreateGC(Xdisplay, menuBar.win,
			      GCForeground,
			      &gcvalue);

	gcvalue.foreground = PixColors[Color_bottomShadow];
	botShadowGC = XCreateGC(Xdisplay, menuBar.win,
				GCForeground | GCFont,
				&gcvalue);

	gcvalue.foreground = PixColors[Color_topShadow];
	topShadowGC = XCreateGC(Xdisplay, menuBar.win,
				GCForeground,
				&gcvalue);
    }
#endif
}

/* PROTO */
void
menubar_expose(void)
{
#ifdef MENUBAR
    menu_t         *menu;
    int             x;

    if (delay_menu_drawing || !menubar_visible())
	return;

/* make sure the font is correct */
    XSetFont(Xdisplay, menubarGC, TermWin.font->fid);
    XSetFont(Xdisplay, botShadowGC, TermWin.font->fid);
    XClearWindow(Xdisplay, menuBar.win);

    menu_hide_all();

    x = 0;
	char           *str, title[256];
	int             len, ncol = TermWin.ncol;
    int xl = 0;
    int xr = TermWin.ncol;
    int rs_pix = 0;

#ifndef NO_MENUBAR_ARROWS
	if (x < (ncol - (NARROWS + 1))) {
	    rs_pix = scrollbar_visible() ? (2 * sb_shadow)+SB_WIDTH : 0;
	    ncol -= (NARROWS + 2);
	    Arrows_x = Width2Pixel(ncol) + rs_pix + Width2Pixel(HSPACE);
	}
	xr = ncol;
#endif
    if (CurrentBar != NULL) {
	for (menu = CurrentBar->head; menu != NULL; menu = menu->next) {
	    int             len = menu->len;

		if (menu->hidden) /* JWT:"HIDDEN" FOR NOW MEANS THE SPECIAL LEFTMOST "PREV. MENUBAR" MENUBUTTON WHEN ONLY 1 MENUBAR: */
	        continue;
	    else if (menu->right_just) {
            x = xr;
            menu->x = x - (menu->len + HSPACE);
            xr = menu->x;
            rs_pix = (menu->right_just && scrollbar_visible()) ? (2 * sb_shadow)+SB_WIDTH : 0;
	    } else {
            menu->x = xl;
            xl = menu->x + (menu->len + HSPACE);
            rs_pix = 0;
	    }

#ifdef DEBUG_MENU_LAYOUT
	    print_menu_descendants(menu);
#endif

	    if (x > TermWin.ncol)
		len = (TermWin.ncol - (menu->x + HSPACE));

	    drawbox_menubar(menu->x, len, +1, rs_pix);
		if (menu->name != NULL && menu->name[0] == '<' && menu->name[1] == 0) { /* SPECIAL MENU NAME "<", DRAW LEFT ARROW!: */
			Draw_Triangle(menuBar.win, botShadowGC, topShadowGC,
				(Width2Pixel(menu->x) + Width2Pixel(HSPACE) / 2) + rs_pix,
				/* menuBar_height() / 2 - MENU_MARGIN, Width2Pixel(1), 'l'); */
				((menuBar_height() - MENU_MARGIN) / 2) - MENU_MARGIN,
				Width2Pixel(1), 'l');
		} else {
			XDrawString(Xdisplay,
				menuBar.win, menubarGC,
				(Width2Pixel(menu->x) + Width2Pixel(HSPACE) / 2) + rs_pix,
				menuBar_height() - MENU_MARGIN,
				menu->name, len);
		}

	    if (x > TermWin.ncol)
		break;
	}
    } else {
        rs_pix = scrollbar_visible() ? sb_shadow+SB_WIDTH : 0;
        drawbox_menubar(0, TermWin.ncol, -1, rs_pix);
    }

/* add the menuBar title, if it exists and there's plenty of room */
    if (xl < ncol) {  /* HAVE ROOM FOR AT LEAST THE ARROWS: */
#ifndef NO_MENUBAR_ARROWS
		draw_Arrows(0, +1);
#endif

		str = (CurrentBar && CurrentBar->title ? CurrentBar->title : "%n-%v");
		for (len = 0; str[0] && len < sizeof(title) - 1; str++) {
		    const char     *s = NULL;

		    switch (str[0]) {
		    case '%':
			str++;
			switch (str[0]) {
			case 'n':
			    s = rs_name;
			    break;	/* resource name */
			case 'v':
			    s = VERSION;
			    break;	/* version number */
			case '%':
			    s = "%";
			    break;	/* literal '%' */
			}
			if (s != NULL)
			    while (*s && len < sizeof(title) - 1)
				title[len++] = *s++;
			break;

		    default:
			title[len++] = str[0];
			break;
		    }
		}
		title[len] = '\0';

		if (len <= 0 || (xr - xl) < len)
			return;  /* SKIP TITLE, NO TITLE OR NO ROOM FOR IT! */

		x = (TermWin.ncol - len) / 2;  /* FIRST TRY CENTERING IN WINDOW: */
		/* IF CENTERING IMPINGES ON BUTTONS, THEN CENTER BETWEEN LEFT & RIGHT BUTTONS: */
		x = (x <= xl || x + len >= xr)
			? Width2Pixel(xl) + (Width2Pixel((xr - xl) - len)) / 2
			: Width2Pixel(x);
	    rs_pix = scrollbar_visible() ? sb_shadow+SB_WIDTH : 0;

	    XDrawString(Xdisplay,
			menuBar.win, menubarGC,
			/* JWT:x WAS Width2Pixel(xl) + Width2Pixel((ncol0 - xr) + HSPACE) / 2, */
			x + (rs_pix / 2),
			menuBar_height() - MENU_MARGIN,
			title, len);
    }
#endif
}

/* PROTO */
int
menubar_mapping(int map)
{
    int             change = 0;

#ifdef MENUBAR
    if (map && !menubar_visible()) {
	menuBar.state = 1;
	XMapWindow(Xdisplay, menuBar.win);
	change = 1;
    } else if (!map && menubar_visible()) {
	menubar_expose();
	menuBar.state = 0;
	XUnmapWindow(Xdisplay, menuBar.win);
	change = 1;
    } else
	menubar_expose();

    if (change)
        resize();
#endif

    return change;
}

/* PROTO */
int
menu_select(XButtonEvent * ev)
{
#ifdef MENUBAR
	menuitem_t     *thisitem, *item = NULL;
	int             this_y, y;

	Window          unused_root, unused_child;
	int             unused_root_x, unused_root_y;
	unsigned int    unused_mask;
	static menuitem_t  *prev_menu_shown;

	if (ActiveMenu == NULL)
		return 0;

	if (ActiveMenu->win != None)
		XQueryPointer(Xdisplay, ActiveMenu->win,
				&unused_root, &unused_child,
				&unused_root_x, &unused_root_y,
				&(ev->x), &(ev->y),
				&unused_mask);

	if (ActiveMenu->parent != NULL && (ev->x < 0 || ev->y < 0)) { /* OUTSIDE OF ALL MENU WINDOWS */
		if (ev->type == ButtonRelease)
			menu_reallyhide_all(NULL);

		if (ActiveMenu != NULL)
			ActiveMenu = ActiveMenu->parent;

		return 1;
	}

	/* determine the menu item corresponding to the Y index */
	if (popupvisible) {  /* JWT:ADDED CODE FOR POPUP MENUS! (TOP HALF OF THIS IF): */
		int h = menu_height();
		y = 0;
 		if (ev->x >= 0) { /* NO SUBMENU WINDOW IS POPPED UP TO RIGHT: */
			for (item = ActiveMenu->head; item != NULL; item = item->next) {
				if (item->entry.type == MenuSubMenu && item->entry.submenu.menu != NULL
						&& item->entry.submenu.menu->hidden)
					continue;  /* JWT:SKIP HIDDEN MENUITEM IN POPUP MENU! */

				h = menu_height();
				if (isSeparator(item->name)) {
					h = SEPARATOR_HEIGHT;
				} else if (ev->y > y && ev->y < (y + h)) {  /* mouse w/n a menu item */
					if (item->name != NULL && item != prev_menu)
						prev_menu = item;

					break;
				}
				y += h;
			}
 		} else {          /* A SUBMENU WINDOW IS POPPED UP TO RIGHT (MOUSE COORDS NOW RELATIVE TO IT!: */
			for (item = CurrentBar->popup->head; item != NULL; item = item->next) {
				if (item->entry.type == MenuSubMenu && item->entry.submenu.menu != NULL
						&& item->entry.submenu.menu->hidden)
					continue;

				if (ev->y > y && ev->y < (y + h)) {
					if (item->name != NULL && item != prev_menu) {
						menu_reallyhide_all(ActiveMenu);
						prev_menu = item;
					} else
						return 0;

 					ActiveMenu = CurrentBar->popup;
					break;
				}
				y += h;
			}
 		}
	} else {
		y = 0;
		if (ev->x >= 0 &&
				ev->x <= (ActiveMenu->w - SHADOW)) {
			for (item = ActiveMenu->head; item != NULL; item = item->next) {
				int             h = menu_height();

				if (isSeparator(item->name))
					h = SEPARATOR_HEIGHT;
				else if (ev->y >= y && ev->y < (y + h))
					break;
				y += h;
			}
		}
	}
	if (item == NULL && ev->type == ButtonRelease) {
		if (ActiveMenu->name != NULL && ActiveMenu->name[0] == '<' && ActiveMenu->name[1] == 0)
			/* JWT:SPECIAL "<"(Previous Menubar) BUTTON: SEND ACTION TO SWITCH TO PREV. MENUBAR: */
			command_dispatch("\E]10;[prev]^G");
		else 
			menu_reallyhide_all(NULL);

		return 0;
	}
	thisitem = item;
	this_y = y;

	/* erase the last item (I *THINK* THIS MEANS UNMASH THE MENUITEM BUTTON ITSELF): */
	if (ActiveMenu->item != NULL) {  /* THE PREVIOUS ITEM IN ACTIVE THE MOUSE WAS ON? */
		if (ActiveMenu->item != thisitem) {
			prev_menu_shown = NULL;

			for (y = 0, item = ActiveMenu->head;
			item != NULL;
			item = item->next)
			{   /* LOOP LOOKING FOR THE "PREV" MENUITEM (TO BE UNPRESSED): */
				int h = menu_height();

				if (item->entry.type == MenuSubMenu && item->entry.submenu.menu != NULL
						&& item->entry.submenu.menu->hidden)
					continue;  /* JWT:SKIP HIDDEN MENUITEM IN POPUP MENU! */

				if (isSeparator(item->name))
					h = SEPARATOR_HEIGHT;
				else if (item == ActiveMenu->item) {  /* FOUND IT (ActiveMenu->item == the PREV item!!: */
					/* erase old menuitem (UNPRESS THE PREVIOUS MENUITEM BUTTON WE WERE ON)? */
					drawbox_menuitem(y, 0);	/* No Shadow */
					if (item->entry.type == MenuSubMenu
							&& item->entry.submenu.menu->name[0] != '<') {
						menu_t *am = ActiveMenu;
						if (!popupvisible)
							drawtriangle(ActiveMenu->w, y, +1, 'r'); /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */

						menu_reallyhide_all(item->entry.submenu.menu);
						ActiveMenu = am;
					}
					break;
				}
				y += h;
			}
		} else {  /* MOUSE STILL OVER THE "CURRENT"(SAME) MENUITEM: */
			switch (ev->type) {
				case ButtonRelease:
				switch (item->entry.type) {
					case MenuSubMenu:  /* WE JUST RELEASED ON A MENUITEM W/A SUBMENU: */
					/* ActiveMenu = item->entry.submenu.menu; */ /* MOVE ACTIVEMENU DOWN TO IT??? (*NOT IN ORIGINAL*): */
					if (item->name != NULL) {
						if (item->name[0] == '<' && item->name[1] == 0) {
							/* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */
							/* JWT:SPECIAL BUTTON *IS* A "MenuSubMenu", W/O A SUBMENU, INSTEAD ACT UPON IT NOW!: */
							/* JWT:SOME DAY, PERHAPS WE CAN MAKE ORDINARY MENUBUTTONS "ACTIONABLE". */
							command_dispatch("\E]10;[prev]^G");
						} else if (item->entry.submenu.menu != NULL) {
							ActiveMenu = item->entry.submenu.menu;
							menu_reallyhide_all(NULL);
							return 0;
						}
					}
					case MenuLabel:
					menu_reallyhide_all(NULL);
					break;

					case MenuAction:  /* MENUITEMS W/O SUBMENUS ARE ALREADY ACTIONABLE: */
					case MenuTerminalAction:
					drawbox_menuitem(this_y, -1);
					/*
							* use select for timing
					* remove menu before sending keys to the application
					*/
					{
#ifdef __CYGWIN32__
						struct timeval tv;

						tv.tv_sec = 0;
						tv.tv_usec = MENU_DELAY_USEC;
						select(0, NULL, NULL, NULL, &tv);
#else
						struct itimerval tv;

						tv.it_value.tv_sec = 0;
						tv.it_value.tv_usec = MENU_DELAY_USEC;
						select(0, NULL, NULL, NULL, &tv.it_value);
#endif
					}
					menu_reallyhide_all(NULL);
#ifndef DEBUG_MENU
					action_dispatch(&(item->entry.action));
#else				/* DEBUG_MENU */
					printf("%s: %s\n", item->name, item->entry.action.str);
#endif				/* DEBUG_MENU */
					break;
				}
				break;

				default:
				if (item->entry.type == MenuSubMenu)
						goto DoMenu;  /* WE ONLY HOVERED OVER ONE W/SUBMENUS (NOT RELEASED ON IT)?: */
				break;
			}
			return 0;
		}
	}

DoMenu:
	ActiveMenu->item = thisitem;
	y = this_y;
	if (thisitem != NULL) {
		item = ActiveMenu->item;
		if (item->entry.type != MenuLabel)
		drawbox_menuitem(y, +1);  /* PRESS IT DOWN? */
		if (item->entry.type == MenuSubMenu) {
			int x;

			if (item->name != NULL && item->name[0] == '<' && item->name[1] == 0)
				return 0;  /* JWT:SPECIAL "<"(Previous Menubar) BUTTON: */
				/* (NO REAL SUBMENUS HERE, SO NO TRIANGLE ARROW)! */

			drawtriangle(ActiveMenu->w, y, -1, 'r'); /* ALL MENUITEMS W/SUBMENUS GET A RIGHT-ARROW! */
			x = ev->x + (ActiveMenu->parent ? ActiveMenu->x : Width2Pixel(ActiveMenu->x));
			if (popupvisible || x >= item->entry.submenu.menu->x) {
				/* JWT:POPUP MENUS ALWAYS POP UP THEIR SUBMENU, OTHERWISE ONLY IF HOVERING OVER RIGHT SIDE: */
				ActiveMenu = item->entry.submenu.menu;  /* SET TO THE SUBMENU A/B TO POP UP. */
				if (item != prev_menu_shown  /* JWT:NOTE:WE'RE ALSO "DIFFERENT" IF SUBMENU GOT HIDDEN!: */
						|| (prev_menu_shown != NULL && !prev_menu_shown->entry.submenu.menu->displayed)) {
					menu_show();  /* THIS DISPLAYS THE MENUITEM'S SUBMENU OUT TO THE RIGHT. */
					prev_menu_shown = item;
				}

				/* JWT:NOTE:BELOW SEEMS NOW TO NEED TO BE ZERO!: */
				return 0;  /* (*NOTE* RETURNS 1 HERE IN ORIGINAL!*) */
			}
		}
	}
#endif
	return 0;
}

/* PROTO */
void
menubar_select(XButtonEvent * ev)
{
#ifdef MENUBAR
	menu_t         *menu = NULL;

	/* determine the pulldown menu corresponding to the X index */
	if (popupvisible) {
		int             y = CurrentBar->popup->y;
		if (ev->x >= CurrentBar->popup->x && ev->x <= CurrentBar->popup->x+CurrentBar->popup->w) {
			if (ev->y > y && ev->y < y + CurrentBar->popup->h)
				return;
		}
	} else {
		if (ev->y >= 0 && ev->y <= menuBar_height() && CurrentBar != NULL) {
			for (menu = CurrentBar->head; menu != NULL; menu = menu->next) {
				int             x = Width2Pixel(menu->x);
				int             w = Width2Pixel(menu->len + HSPACE);
				if (menu->hidden)
					continue;  /* JWT:SKIP HIDDEN MENUBUTTON - NOT SELECTABLE. */

				x += (menu->right_just && scrollbar_visible()) ? sb_shadow+SB_WIDTH : 0;
				if (ev->x >= x && ev->x < x + w)
					break;
			}
		}
	}
	switch (ev->type) {
		case ButtonRelease:
		menu_reallyhide_all(NULL);
		break;
#ifndef NO_MENUBAR_ARROWS
		case ButtonPress:
		if (menu == NULL && Arrows_x && ev->x >= Arrows_x) {
			for (int i = 0; i < NARROWS; i++) {
				if (ev->x >= (Arrows_x + (Width2Pixel(4 * i + i)) / 4) &&
						ev->x < (Arrows_x + (Width2Pixel(4 * i + i + 4)) / 4)) {
					draw_Arrows(Arrows[i].name, -1);
					/*
							* use select for timing
					*/
					{
#ifdef __CYGWIN32__
						struct timeval tv;

						tv.tv_sec = 0;
						tv.tv_usec = MENU_DELAY_USEC;
						select(0, NULL, NULL, NULL, &tv);
#else
						struct itimerval tv;

						tv.it_value.tv_sec = 0;
						tv.it_value.tv_usec = MENU_DELAY_USEC;
						select(0, NULL, NULL, NULL, &tv.it_value);
#endif
					}
					draw_Arrows(Arrows[i].name, +1);
#ifdef DEBUG_MENUARROWS
					printf("'%c': ", Arrows[i].name);

					if (CurrentBar == NULL ||
							(CurrentBar->arrows[i].type != MenuAction &&
							CurrentBar->arrows[i].type != MenuTerminalAction))
					{
						if (Arrows[i].str[0])
							printf("(default) \\033%s\n", &(Arrows[i].str[2]));
					} else {
						printf("%s\n", CurrentBar->arrows[i].str);
					}
#else					/* DEBUG_MENUARROWS */
					if (CurrentBar == NULL || action_dispatch(&(CurrentBar->arrows[i]))) {
						if (Arrows[i].str[0] != 0)
							tt_write((Arrows[i].str + 1),
						Arrows[i].str[0]);
					}
#endif					/* DEBUG_MENUARROWS */
					return;
				}
			}
		}
#endif
		/*drop */

		default:
		/*
				* press menubar or move to a new entry
		*/
		if (menu != NULL && menu != ActiveMenu) {
			menu_hide_all();	/* pop down old menu */
			if (ActiveMenu == menu)
				return;

			ActiveMenu = menu;
			menu_show();	/* pop up new menu */
		}
		break;
	}
	return;
#endif
}

/*
 * general dispatch routine,
 * it would be nice to have `sticky' menus
 */
/* PROTO */
void
menubar_control(XButtonEvent * ev)
{
#ifdef MENUBAR
    switch (ev->type) {
    case ButtonPress:
	if (ev->button == Button1)
	    menubar_select(ev);
	break;

    case ButtonRelease:
	if (ev->button == Button1)
	    menu_select(ev);
	if (popupvisible)
	    menu_reallyhide_all(NULL);
	break;

    case MotionNotify:
	while (XCheckTypedWindowEvent(Xdisplay, TermWin.parent,
				      MotionNotify,
				      (XEvent *) ev)) ;

	if (ActiveMenu)

	    while (menu_select(ev)) ;  /* JWT:CONFUSING, BUT SEEMS TO WORK?! */

	else
	    ev->y = -1;

	if (ev->y < 0) {
	    Window          unused_root, unused_child;
	    int             unused_root_x, unused_root_y;
	    unsigned int    unused_mask;

	    XQueryPointer(Xdisplay, menuBar.win,
			  &unused_root, &unused_child,
			  &unused_root_x, &unused_root_y,
			  &(ev->x), &(ev->y),
			  &unused_mask);
	    menubar_select(ev);
	}
	break;
    }
#endif
}

/* PROTO */
void
map_menuBar(int map)
{
#ifdef MENUBAR
    if (delay_menu_drawing) {
	delay_menu_drawing++;
    } else if (menubar_mapping(map))
    resize();
#endif
}

/* PROTO */
void
create_menuBar(Cursor cursor)
{
#ifdef MENUBAR
/* menuBar: size doesn't matter */
    menuBar.win = XCreateSimpleWindow(Xdisplay, TermWin.parent,
				      0, 0,
				      1, 1,
				      0,
				      PixColors[Color_fg],
				      PixColors[Color_scroll]);
    XDefineCursor(Xdisplay, menuBar.win, cursor);
    XSelectInput(Xdisplay, menuBar.win,
		 (ExposureMask | ButtonPressMask | ButtonReleaseMask |
		  Button1MotionMask));

    menu_cursor = cursor;  /* JWT:GRAB THE CURSOR FOR OUR USE. */
#endif
}

/* PROTO */
void
Resize_menuBar(int x, int y, unsigned int width, unsigned int height)
{
#ifdef MENUBAR
    XMoveResizeWindow(Xdisplay, menuBar.win, x, y, width, height);	
#endif
}

/*----------------------- end-of-file (C source) -----------------------*/
