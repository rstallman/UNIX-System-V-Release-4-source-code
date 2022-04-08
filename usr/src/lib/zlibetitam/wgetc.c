/*	Copyright (c) 1984 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)stitam:wgetc.c	1.6"
/**************************************************************************
 *                                                                        *
 * name:  TAMwgetc( window )                                              *
 *                                                                        *
 * function:  TAMwgetc() is the path for input from a keyboard to a TAM   *
 *            application program using wgetc().  This routine has the    *
 *            following important properties:                             *
 *                                                                        *
 *    1.  All escape sequences that can be generated by the UNIX PC       *
 *        are recognized and returned to an applications program in their *
 *        eight bit virtual form as defined on the UNIX PC.               *
 *                                                                        *
 *    2.  Escape sequences that are keyboard specific, such as arrows, are*
 *        returned to an applications program as their UNIX PC equivalent.*
 *                                                                        *
 *    3.  Escape sequences for a given keyboard whose virtualization does *
 *        does not have an equivalent on the UNIX PC are returned to an   *
 *        application program as their selves.                            *
 *                                                                        *
 **************************************************************************/
 
#include "cvttam.h"

#define	MaxEscape	3	/* Maximum length escape sequence */
#define	ABORT		-1

typedef struct
{
	int	head,tail;
	chtype	AbortArena[MaxEscape];

} WindowBuffer;

static WindowBuffer	wbuf[NWINDOW];
extern int		Keypad;
extern char		*Virtual2Ansi();

int	TAMwgetc( w )

short	w;
{
	WindowBuffer	*bufp;
	TAMWIN		*wp;
	int		answer;

	TAMwrefresh (w);
	if( !(wp=_validwindow(w)) ) return ERR;

	bufp = &wbuf[w];

/***
 *** Check to see if during the last read to the window an unknown
 *** escape sequence was entered by the user.  If so, then return the
 *** next character of this sequence to the caller.
 ***/
	if( bufp->head != bufp->tail ) {
		answer = bufp->AbortArena[bufp->tail];
		bufp->tail++;
		if( bufp->tail == bufp->head) {
			bufp->head = 0;
			bufp->tail = 0;
		}
		return answer;

	}
	else {
		answer = wgetch( Scroll(wp) );
		if( answer == ERR ) {
			if( !(State(wp) & NODELAY) )
				return ERR;
			else
				return EOF;
		}
		else {
			bufp->AbortArena[bufp->head++] = answer;
			bufp->tail = bufp->head;
			answer = ReadMagic( answer );

			switch( answer ) {
/***
 ***	0       == Valid Escape sequence has not been decoded yet
 ***	ABORT   == An unknown escape sequence has been detected.  flush your buffers.
 ***	default == Valid Escape sequence has been decoded.
 ***/
			case 0:
				return TAMwgetc( w );
			case ABORT:
				bufp->tail = 0;
				return TAMwgetc( w );
			default:
				if (Keypad == 2) {
					bufp->tail = 0;
					return TAMwgetc( w );;
				}
				bufp->head = bufp->tail = 0;
				break;
			}
		}
	}

/***
 *** This section of code translates virtual keyboard keys (from curses) into
 *** their UNIX PC escape sequences.
 ***/
	if( !Keypad ) {
		char	*s;

		if( !(s=Virtual2Ansi(answer))) return answer;
		answer = *s;
		for(s++; *s; s++) {
			bufp->AbortArena[bufp->head++] = *s;
		}
	}
	return answer;
}

