/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)libc-port:gen/cfgetispeed.c	1.1"

#ifdef __STDC__
	#pragma weak cfgetispeed = _cfgetispeed
#endif
#include "synonyms.h"
#include <sys/termios.h>

/*
 * returns input baud rate stored in c_cflag pointed by termios_p
 */

speed_t cfgetispeed(termios_p)
struct termios *termios_p;
{
	return ((termios_p->c_cflag & CIBAUD) >> 16);
}
