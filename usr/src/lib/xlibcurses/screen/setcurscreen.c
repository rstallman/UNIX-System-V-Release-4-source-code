/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curses:screen/setcurscreen.c	1.3"
#include "curses_inc.h"

SCREEN	*
setcurscreen(new)
SCREEN	*new;
{
    register	SCREEN	*rv = SP;

    if (new != SP)
    {

#ifdef	DEBUG
	if (outf)
	    fprintf(outf, "setterm: old %x, new %x\n", rv, new);
#endif	/* DEBUG */

	SP = new;
	(void) setcurterm(SP->tcap);
	LINES = SP->lsize;
	COLS = SP->csize;
	TABSIZE = SP->tsize;
	stdscr = SP->std_scr;
	curscr = SP->cur_scr;
	_virtscr = SP->virt_scr;
    }
    return (rv);
}
