/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curses:screen/_set_curterm.c	1.2"

#define	NOMACROS
#include	"curses_inc.h"

TERMINAL *
set_curterm(newterminal)
TERMINAL *newterminal;
{
    return (setcurterm(newterminal));
}
