/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curses:has_colors.c	1.1"

#include "curses_inc.h"

bool has_colors()
{
    return ((max_pairs == -1) ? FALSE : TRUE);
}
