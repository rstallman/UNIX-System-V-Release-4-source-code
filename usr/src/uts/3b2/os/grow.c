/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)kernel:os/grow.c	1.33"
#include "sys/types.h"
#include "sys/bitmasks.h"
#include "sys/param.h"
#include "sys/psw.h"
#include "sys/sysmacros.h"
#include "sys/immu.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/lock.h"
#include "sys/var.h"
#include "sys/proc.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#include "sys/cmn_err.h"
#include "sys/sbd.h"
#include "sys/cred.h"
#include "sys/vnode.h"
#include "sys/vm.h"
#include "sys/file.h"
#include "sys/mman.h"

#include "vm/hat.h"
#include "vm/as.h"
#include "vm/page.h"
#include "vm/seg.h"
#include "vm/seg_dev.h"
#include "vm/seg_vn.h"

struct brka {
	caddr_t	nva;
};

/* ARGSUSED */
int
brk(uap, rvp)
	struct brka *uap;
	rval_t *rvp;
{
	size_t size = uap->nva - u.u_procp->p_brkbase;

#ifdef DEBUG
	int rval;

	rval = db_brk(size);
	if (rval) sysoops();
	return(rval);
}

int
db_brk(size)
	register size_t size;
{
#endif

	register caddr_t nva;			/* current break address */
	register caddr_t ova;			/* current break address */
	register int	change;			/* change in pages */
	register int	error;
	register int 	lckflag = 0;
	register struct proc *p = u.u_procp;

	nva = (caddr_t)roundup((u_int)(p->p_brkbase + size), PAGESIZE);
	ova = (caddr_t)roundup((u_int)(p->p_brkbase + p->p_brksize), PAGESIZE);

	if (nva > (caddr_t) UVUBLK 
	  || size < 0
	  || size > u.u_rlimit[RLIMIT_DATA].rlim_cur)
		return ENOMEM;

	change = nva - ova;

	if (change > 0) {

		if (u.u_lock & (DATLOCK | PROCLOCK)) {
			if (p->p_as->a_paglck == 0) {
				 p->p_as->a_paglck = 1;
					 lckflag = 1;
			}
		}

		/*
		 * Add new zfod mapping to extend UNIX data segment
		 */
		error = as_map(p->p_as, ova, (u_int)change, 
		 	       segvn_create, zfod_argsp); 
		if (lckflag)
			p->p_as->a_paglck = 0;

		if (error)
			return error;

	} else if (change < 0) {
		/*
		 * Can't shrink the data space if there are any outstanding
		 * async I/O operations.
		 */
		if (p->p_aiocount)
			return EINVAL;

		/*
		 * Release mapping to shrink UNIX data segment.
		 * NOTE: change is negative.
		 */
		(void) as_unmap(p->p_as, ova + change, (u_int)(-change));
	}

	p->p_brksize = size;
	return 0;
}

/*
 * Grow the stack to include the SP.  Return 1 if successful, 0 otherwise.
 * This routine is machine dependent.
 */
int
grow(sp)
	int *sp;
{
	register int	si;
	register struct proc *p = u.u_procp;
	register int	ssize;
	register int 	lckflag = 0;

	ssize = btoc(p->p_stksize*sizeof(int *));
	si = btoc((u_int)sp - (u_int)p->p_stkbase) - ssize + SINCR;

	if (si == 0 || (u_int) sp >= UVSHM)
		return 0;

	if (si > 0) {
		if (ctob(ssize + si) > u.u_rlimit[RLIMIT_STACK].rlim_cur)
			return 0;

		if (u.u_lock & (DATLOCK | PROCLOCK)) {
			if (p->p_as->a_paglck == 0) {
				 p->p_as->a_paglck = 1;
				 lckflag = 1;
			}
		}
		if (as_map(p->p_as, (caddr_t)p->p_stkbase + ctob(ssize),
		    (u_int)ctob(si), segvn_create, zfod_argsp) != 0) {
			if (lckflag)
				p->p_as->a_paglck = 0;
			return (0);
		}
		if (lckflag)
			p->p_as->a_paglck = 0;
	} else {
		/*
		 * Release mapping to shrink UNIX stack segment
		 */
		(void) as_unmap(p->p_as, 
		  (caddr_t)p->p_stkbase + ctob(ssize+si), (u_int)ctob(-si));
	}

	p->p_stksize += howmany(ctob(si), sizeof(int *));
	u.u_pcb.sub = p->p_stkbase + p->p_stksize;

	return (1);

}

/* ARGSUSED */
int
getpagesize(uap, rvp)
	rval_t *rvp;
{

	rvp->r_val1 = PAGESIZE;
	return (0);
}

struct mmapa {
	caddr_t	addr;
	size_t	len;
	int	prot;
	int	flags;
	int	fd;
	off_t	pos;
};

int
mmap(uap, rvp)
	register struct mmapa *uap;
	rval_t *rvp;
{
	register struct vnode *vp;
	caddr_t addr;
	register size_t len;
	struct as *as = u.u_procp->p_as;
	struct file *fp;
	u_int prot, maxprot;
	u_int type;
	register u_int flags;
	register int error;

	flags = uap->flags;
	type = flags & MAP_TYPE;

	if ((flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_FIXED 
	    | MAP_NORESERVE	/* not implemented, but don't fail here */
	    /* | MAP_RENAME */	/* not yet implemented, let user know */
	    )) != 0) {
		return(EINVAL);
	}

        if (type != MAP_PRIVATE && type != MAP_SHARED) {
		return(EINVAL);
	}

	len = uap->len;
	addr = uap->addr;

	/*
	 * Check for bad lengths and file position.
	 * We let the VOP_MAP routine check for negative lengths
	 * since on some vnode types this might be appropriate.
	 */
	if ((uap->pos & PAGEOFFSET) != 0) {
		return(EINVAL);
	}

	if (error = getf(uap->fd, &fp))
		return(error);

	vp = fp->f_vnode;

	maxprot = PROT_ALL;		/* start out allowing all accesses */
	prot = uap->prot | PROT_USER;

	if (type == MAP_SHARED && (fp->f_flag & FWRITE) == 0) {
		/* no write access allowed */
		maxprot &= ~PROT_WRITE;
	}

	/*
	 * XXX - Do we also adjust maxprot based on protections
	 * of the vnode?  E.g. if no execute permission is given
	 * on the vnode for the current user, maxprot probably
	 * should disallow PROT_EXEC also?  This is different
	 * from the write access as this would be a per vnode
	 * test as opposed to a per fd test for writability.
	 */

	/*
	 * Verify that the specified protections are not greater than
	 * the maximum allowable protections.  Also test to make sure
	 * that the file descriptor does allows for read access since
	 * "write only" mappings are hard to do since normally we do
	 * the read from the file before the page can be written.
	 */
	if (((maxprot & prot) != prot) || (fp->f_flag & FREAD) == 0) {
		return(EACCES);
	}

	/*
	 * If the user specified an address, do some simple checks here
	 */
	if ((flags & MAP_FIXED) != 0) {
		/*
		 * Use the user address.  First verify that
		 * the address to be used is page aligned.
		 * Then make some simple bounds checks.
		 */
		if (((int)addr & PAGEOFFSET) != 0) {
			return(EINVAL);
		}
		if (valid_usr_range(addr, len) == 0) {
			return(ENOMEM);
		}
	}

	/*
	 * Ok, now let the vnode map routine do its thing to set things up.
	 */
	if (error = VOP_MAP(vp, uap->pos, as, &addr, len, prot, maxprot, flags,
	    fp->f_cred))
		return(error);

	rvp->r_val1 = (int)addr;	/* return starting address */
	return (0);
}

struct munmapa {
	caddr_t	addr;
	size_t	len;
};

/* ARGSUSED */
int
munmap(uap, rvp)
	struct munmapa *uap;
	rval_t *rvp;
{
	register caddr_t addr = uap->addr;
	register size_t len = uap->len;
	register struct proc *p = u.u_procp;

	if (((int)addr & PAGEOFFSET) != 0) {
		return(EINVAL);
	}

	if (valid_usr_range(addr, len) == 0) {
		return(EINVAL);
	}

	if (as_unmap(p->p_as, addr, len) != 0) {
		return(EINVAL);
	}

	return (0);
}

struct mctla {
	caddr_t	addr;
	size_t	len;
	int	function;
	caddr_t	arg;
};

/*
 * "Memory-control" operations: things that affect the "VM cache".
 */

/* ARGSUSED */
int
mctl(uap, rvp)
	register struct mctla *uap;
	rval_t *rvp;
{
	register caddr_t addr = uap->addr;
	register size_t len = uap->len;
	register caddr_t arg = uap->arg;
	int error = 0;

	if ((uap->function == MC_LOCKAS) || (uap->function == MC_UNLOCKAS)) {
		if ((addr != 0) || (len != 0)) {
			return EINVAL;
		}
	} else {
		if (((int)addr & PAGEOFFSET) != 0) {
			return(EINVAL);
		}
		if (valid_usr_range(addr, len) == 0) {
			return(ENOMEM);
		}
	}
	switch (uap->function) {
	case MC_SYNC:
		if ((int)arg & ~(MS_ASYNC|MS_INVALIDATE)) 
			return EINVAL;
		else {
			error = as_ctl(u.u_procp->p_as, addr, len, 
			  uap->function, 0, arg, (u_long *)NULL, (size_t)NULL);
		}
		break;
	case MC_LOCKAS:
		if ((int)arg & ~(MCL_FUTURE|MCL_CURRENT) || (int)arg == 0) 
			return EINVAL;
		/* FALLTHROUGH */
	case MC_UNLOCKAS:
	case MC_LOCK:
	case MC_UNLOCK:
		if (!suser(u.u_cred))
			error = EPERM;
		else {
			error = as_ctl(u.u_procp->p_as, addr, len,
			  uap->function, 0, arg, (u_long *)NULL, (size_t)NULL);
		}
		break;

	/*
	 * Implement advice -- goes here someday.
	 */
	case MC_ADVISE:
		/* drop through for the moment */
	default:
		error = EINVAL;
	}
	return error;
}

struct mprotecta {
	caddr_t	addr;
	size_t	len;
	int	prot;
};

/* ARGSUSED */
int
mprotect(uap, rvp)
	struct mprotecta *uap;
	rval_t *rvp;
{
	register caddr_t addr = uap->addr;
	register size_t len = uap->len;
	register u_int prot = uap->prot | PROT_USER;
	register int error;

	if (((int)addr & PAGEOFFSET) != 0) {
		return(EINVAL);
	}

	if (valid_usr_range(addr, len) == 0) {
		return(ENOMEM);
	}

	error = as_setprot(u.u_procp->p_as, addr, len, prot);
	return(error);
}

struct madvisea {
	caddr_t	addr;
	size_t len;
	int behav;
};

/* ARGSUSED */
int
madvise(uap, rvp)
	register struct madvisea *uap;
	rval_t *rvp;
{
	faultcode_t fc;
	register caddr_t addr = uap->addr;
	register size_t len = uap->len;
	register struct as *as = u.u_procp->p_as;
	register int error = 0;

	if (valid_usr_range(addr, len) == 0) {
		return(ENOMEM);
	}

	switch (uap->behav) {

	case MADV_NORMAL:
		/*
		 * Should inform segment driver to go back to read-ahead
		 * and no free behind the current address on faults.
		 */
		break;

	case MADV_RANDOM:
		/*
		 * Should inform segment driver to have no read-ahead
		 * and no free behind the current address on faults.
		 */
		break;

	case MADV_SEQUENTIAL:
		/*
		 * Should inform segment driver to do read-ahead and
		 * to free behind the current address on faults.
		 */
		break;

	case MADV_WILLNEED:
		fc = as_faulta(as, addr, (u_int)len);
		if (fc) {
			if (FC_CODE(fc) == FC_OBJERR)
				error = FC_ERRNO(fc);
			else
				error = EINVAL;
		}
		break;

	case MADV_DONTNEED:
		/*
		 * For now, don't need is turned into an as_ctl(MC_SYNC)
		 * operation flagged for async invalidate.
		 */
		error = as_ctl(as, addr, len, MC_SYNC, 0,
	            (caddr_t) (MS_ASYNC | MS_INVALIDATE),
		    	(u_long *)NULL, (size_t)NULL);
		break;

	default:
		/* unknown behavior value */
		error = EINVAL;
		break;

	}
	return(error);
}

#define	MC_CACHE	128			/* internal result buffer */
#define	MC_QUANTUM	(MC_CACHE * PAGESIZE)	/* addresses covered in loop */

struct mincorea {
	caddr_t addr;
	size_t	len;
	char	*vec;
};

/* ARGSUSED */
int
mincore(uap, rvp)
	struct mincorea *uap;
	rval_t *rvp;
{
	register caddr_t addr = uap->addr;
	register caddr_t ea;			/* end address of loop */
	register struct as *as;			/* address space */
	u_int rl;				/* inner result length */
	char vec[MC_CACHE];			/* local vector cache */
	register int error;
	size_t	len = uap->len;

	/*
	 * Validate form of address parameters.
	 */
	if (((int)addr & PAGEOFFSET) != 0) {
		return(EINVAL);
	}

	if (valid_usr_range(addr, len) == 0) {
		return(ENOMEM);
	}

	/*
	 * Loop over subranges of interval [addr : addr + len), recovering
	 * results internally and then copying them out to caller.  Subrange
	 * is based on the size of MC_CACHE, defined above.
	 */
	as = u.u_procp->p_as;
	for (ea = addr + len; addr < ea; addr += MC_QUANTUM) {
		error = as_incore(as, addr, 
		    (u_int)MIN(MC_QUANTUM, ea - addr), vec, &rl);
		if (rl != 0) {
			rl = (rl + PAGESIZE - 1) / PAGESIZE;
			if ((error = copyout(vec, uap->vec, rl)) != 0)
				return(EFAULT);
			uap->vec += rl;
		}
		if (error != 0) {
			return(ENOMEM);
		}
	}
	return(0);
}
