/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _FS_S5FBLK_H
#define _FS_S5FBLK_H

#ident	"@(#)head.sys:sys/fs/s5fblk.h	11.2"
struct	fblk
{
	int	df_nfree;
	daddr_t	df_free[NICFREE];
};

#endif	/* _FS_S5FBLK_H */
