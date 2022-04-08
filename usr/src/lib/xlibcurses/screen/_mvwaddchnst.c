/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curses:screen/_mvwaddchnst.c	1.1"

#define	NOMACROS

#include	"curses_inc.h"

mvwaddchnstr(win, y, x, ch, n)
WINDOW	*win;
int	y, x, n;
chtype	*ch;
{
    return (wmove(win, y, x)==ERR?ERR:waddchnstr(win, ch, n));
}
