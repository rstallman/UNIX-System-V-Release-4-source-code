/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)kernel:os/getsizes.c	1.9"
#include "sys/types.h"
#include "sys/param.h"
#include "sys/psw.h"
#include "sys/immu.h"
#include "sys/buf.h"
#include "sys/tty.h"
#include "sys/vfs.h"
#include "sys/cred.h"
#include "sys/vnode.h"
#include "sys/file.h"
#include "sys/proc.h"
#include "sys/map.h"
#include "sys/callo.h"
#include "sys/fcntl.h"
#include "sys/flock.h"
#include "sys/pcb.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/var.h"
#include "sys/tuneable.h"
#include "sys/conf.h"
#include "sys/ipc.h"
#include "sys/msg.h"
#include "sys/sem.h"
#include "sys/shm.h"
#include "sys/swap.h"
#include "sys/iobuf.h"
#include "sys/nserve.h"
#include "sys/dirent.h"
#include "sys/stream.h"
#include "sys/strsubr.h"
#include "sys/session.h"
#include "sys/resource.h"

#include "sys/evecb.h"
#include "sys/hrtcntl.h"
#include "sys/hrtsys.h"
#include "sys/priocntl.h"
#include "sys/procset.h"
#include "sys/events.h"
#include "sys/evsyscall.h"
#include "sys/evsys.h"

#include "sys/disp.h"
#include "sys/class.h"

#include "vm/as.h"
#include "vm/seg.h"
#include "vm/page.h"
#include "vm/seg_vn.h"
