/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fs:fs/s5/s5vnops.c	1.8.1.30"
#include "sys/types.h"
#include "sys/buf.h"
#include "sys/cmn_err.h"
#include "sys/conf.h"
#include "sys/cred.h"
#include "sys/debug.h"
#include "sys/dirent.h"
#include "sys/errno.h"
#include "sys/time.h"
#include "sys/fbuf.h"
#include "sys/fcntl.h"
#include "sys/file.h"
#include "sys/flock.h"
#include "sys/kmem.h"
#include "sys/immu.h"
#include "sys/inline.h"
#include "sys/mman.h"
#include "sys/open.h"
#include "sys/param.h"
#include "sys/pathname.h"
#include "sys/sysinfo.h"
#include "sys/sysmacros.h"
#include "sys/systm.h"
#include "sys/uio.h"
#include "sys/var.h"
#include "sys/vfs.h"
#include "sys/vnode.h"

#include "sys/proc.h"
#include "sys/disp.h"
#include "sys/user.h"

#include "sys/fs/s5param.h"
#include "sys/fs/s5dir.h"
#include "sys/fs/s5filsys.h"
#include "sys/fs/s5inode.h"
#include "sys/fs/s5macros.h"

#include "vm/page.h"
#include "vm/pvn.h"
#include "vm/as.h"
#include "vm/seg.h"
#include "vm/seg_map.h"
#include "vm/seg_vn.h"
#include "vm/rm.h"

#include "fs/fs_subr.h"

#define	ISVDEV(t) ((t) == VCHR || (t) == VBLK || (t) == VFIFO || (t) == VXNAM)

/*
 * UNIX file system operations vector.
 */
STATIC int	s5open(), s5close(), s5read(), s5write(), s5ioctl();
STATIC int	s5getattr(), s5setattr(), s5access(), s5lookup();
STATIC int	s5create(), s5remove(), s5link(), s5rename();
STATIC int	s5mkdir(), s5rmdir(), s5readdir();
STATIC int	s5symlink(), s5readlink(), s5fsync(), s5fid();
STATIC int	s5seek(), s5frlock(), s5space();
STATIC int	s5getpage(), s5putpage(), s5map(), s5addmap(), s5delmap();
STATIC int	s5bloop(), s5writelbn();

STATIC void	s5inactive(), s5rwlock(), s5rwunlock();
extern int fs_setfl();

struct vnodeops s5vnodeops = {
	s5open,
	s5close,
	s5read,
	s5write,
	s5ioctl,
	fs_setfl,
	s5getattr,
	s5setattr,
	s5access,
	s5lookup,
	s5create,
	s5remove,
	s5link,
	s5rename,
	s5mkdir,
	s5rmdir,
	s5readdir,
	s5symlink,
	s5readlink,
	s5fsync,
	s5inactive,
	s5fid,
	s5rwlock,
	s5rwunlock,
	s5seek,
	fs_cmp,
	s5frlock,
	s5space,
	fs_nosys,	/* realvp */
	s5getpage,
	s5putpage,
	s5map,
	s5addmap,
	s5delmap,
	fs_poll,
	fs_nosys,	/* dump */
	fs_nosys,	/* filler */
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
};

/*
 * No special action required for ordinary files.  (Devices are handled
 * through the device file system.)
 */
/* ARGSUSED */
STATIC int
s5open(vpp, flag, cr)
	struct vnode **vpp;
	int flag;
	struct cred *cr;
{
	return 0;
}

/* ARGSUSED */
STATIC int
s5close(vp, flag, count, offset, cr)
	register struct vnode *vp;
	int flag;
	int count;
	off_t offset;
	struct cred *cr;
{
	register struct inode *ip = VTOI(vp);

	ILOCK(ip);
	ITIMES(ip);
	cleanlocks(vp, u.u_procp->p_epid, u.u_procp->p_sysid);
	IUNLOCK(ip);
	return 0;
}

/* ARGSUSED */
STATIC int
s5read(vp, uiop, ioflag, cr)
	register struct vnode *vp;
	register struct uio *uiop;
	int ioflag;
	struct cred *cr;
{
	struct inode *ip = VTOI(vp);
	int error;

	ASSERT((ip->i_flag & IRWLOCKED)
	  || (vp->v_type != VREG && vp->v_type != VDIR));
	error = readi(ip, uiop, ioflag);
	ITIMES(ip);
	return error;
}

/* ARGSUSED */
STATIC int
s5write(vp, uiop, ioflag, cr)
	register struct vnode *vp;
	register struct uio *uiop;
	int ioflag;
	struct cred *cr;
{
	struct inode *ip = VTOI(vp);
	int error;

	ASSERT((ip->i_flag & IRWLOCKED)
	  || (vp->v_type != VREG && vp->v_type != VDIR));

	if (vp->v_type == VREG
	  && (error = fs_vcode(vp, &ip->i_vcode)))
		return error;
	if ((ioflag & IO_APPEND) && vp->v_type == VREG)
		uiop->uio_offset = ip->i_size;
	error = writei(ip, uiop, ioflag);
	ILOCK(ip);
	ITIMES(ip);
	if ((ioflag & IO_SYNC) && vp->v_type == VREG) {
		/* 
		 * If synchronous write, update inode now.
		 */
		ip->i_flag |= ISYN;
		iupdat(ip);
	}
	IUNLOCK(ip);
	return error;
}

/* ARGSUSED */
STATIC int
s5ioctl(vp, cmd, arg, flag, cr, rvalp)
	register struct vnode *vp;
	int cmd;
	int arg;
	int flag;
	struct cred *cr;
	int *rvalp;
{
	return ENOTTY;
}

STATIC int s5getsp();

/* ARGSUSED */
STATIC int
s5getattr(vp, vap, flags, cr)
	register struct vnode *vp;
	register struct vattr *vap;
	int flags;
	struct cred *cr;
{
	register struct inode *ip = VTOI(vp);
	int error = 0;

	/*
	 * Return (almost) all the attributes.  This should be refined so
	 * that it only returns what's asked for.
	 */
	ILOCK(ip);
	ITIMES(ip);
	vap->va_type = vp->v_type;
	vap->va_mode = ip->i_mode & MODEMASK;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_fsid = ip->i_dev;
	vap->va_nodeid = ip->i_number;
	vap->va_nlink = ip->i_nlink;
	vap->va_size = ip->i_size;
	if (vp->v_type == VCHR || vp->v_type == VBLK || vp->v_type == VXNAM)
		vap->va_rdev = ip->i_rdev;
	else
		vap->va_rdev = 0;	/* not a b/c spec. */
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_nsec = 0;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_nsec = 0;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_nsec = 0;
	vap->va_type = vp->v_type;
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		vap->va_blksize = MAXBSIZE;
	else
		vap->va_blksize = VBSIZE(vp);
	vap->va_vcode = ip->i_vcode;
	if (vap->va_mask & AT_NBLOCKS)
		error = s5getsp(vp, &vap->va_nblocks);
	else
		vap->va_nblocks = 0;
	IUNLOCK(ip);
	return error;
}

STATIC int
s5setattr(vp, vap, flags, cr)
	register struct vnode *vp;
	register struct vattr *vap;
	int flags;
	register struct cred *cr;
{
	int error = 0;
	int newvcode = 0;
	register long int mask = vap->va_mask;
	register struct inode *ip = VTOI(vp);

	/*
	 * Cannot set the attributes represented by AT_NOSET.
	 */
	if (mask & AT_NOSET)
		return EINVAL;

	IRWLOCK(ip);
	ILOCK(ip);

	/*
	 * Change file access modes.  Must be owner or super-user.
	 */
	if (mask & AT_MODE) {
		if (cr->cr_uid != ip->i_uid && !suser(cr)) {
			error = EPERM;
			goto out;
		}
		ip->i_mode &= IFMT;
		ip->i_mode |= vap->va_mode & ~IFMT;
		if (cr->cr_uid != 0) {
			/*
			 * A non-privileged user can set the sticky bit
			 * on a directory.
			 */
			if (vp->v_type != VDIR)
				ip->i_mode &= ~ISVTX;
			if (!groupmember(ip->i_gid, cr))
				ip->i_mode &= ~ISGID;
		}
		ip->i_flag |= ICHG;
		if ((vp->v_flag & VTEXT) && ((ip->i_mode & ISVTX) == 0))
			xrele(vp);
		if (MANDLOCK(vp, vap->va_mode)) {
			if (error = fs_vcode(vp, &ip->i_vcode))
				goto out;
			newvcode = 1;
		}
	}
	/*
	 * Change file ownership; must be the owner of the file
	 * or the super-user.  If the system was configured with
	 * the "rstchown" option, the owner is not permitted to
	 * give away the file, and can change the group id only
	 * to a group of which he or she is a member.
	 */
	if (mask & (AT_UID|AT_GID)) {
		int checksu = 0;

		if (rstchown) {
			if (((mask & AT_UID) && vap->va_uid != ip->i_uid)
			  || ((mask & AT_GID) && !groupmember(vap->va_gid, cr)))
				checksu = 1;
		} else if (cr->cr_uid != ip->i_uid)
			checksu = 1;

		if (checksu && !suser(cr)) {
			error = EPERM;
			goto out;
		}

		if (cr->cr_uid != 0)
			ip->i_mode &= ~(ISUID|ISGID);
		if (mask & AT_UID)
			ip->i_uid = vap->va_uid;
		if (mask & AT_GID)
			ip->i_gid = vap->va_gid;
		ip->i_flag |= ICHG;
	}
	/*
	 * Truncate file.  Must have write permission and not be a directory.
	 */
	if (mask & AT_SIZE) {
		if (vap->va_size != 0) {
			error = EINVAL;
			goto out;
		}
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
		if (error = iaccess(ip, IWRITE, cr))
			goto out;
		
		if (vp->v_type == VREG && !newvcode
		  && (error = fs_vcode(vp, &ip->i_vcode)))
			goto out;
		itrunc(ip);
	}
	/*
	 * Change file access or modified times.
	 */
	if (mask & (AT_ATIME|AT_MTIME)) {
		if (cr->cr_uid != ip->i_uid && cr->cr_uid != 0) {
			if (flags & ATTR_UTIME)
				error = EPERM;
			else
				error = iaccess(ip, IWRITE, cr);
			if (error)
				goto out;
		}
		if (mask & AT_ATIME) {
			ip->i_atime = vap->va_atime.tv_sec;
			ip->i_flag &= ~IACC;
		}
		if (mask & AT_MTIME) {
			ip->i_mtime = vap->va_mtime.tv_sec;
			ip->i_flag &= ~IUPD;
			ip->i_flag |= IMODTIME;
		}
		ip->i_ctime = hrestime.tv_sec;
		ip->i_flag |= ICHG;
	}
out:
	if (!(flags & ATTR_EXEC)) {
		ip->i_flag |= ISYN;
		iupdat(ip);	/* XXX should be async for performance */
	}
	IUNLOCK(ip);
	IRWUNLOCK(ip);
	return error;
}

/* ARGSUSED */
STATIC int
s5access(vp, mode, flags, cr)
	struct vnode *vp;
	int mode;
	int flags;
	struct cred *cr;
{
	struct inode *ip = VTOI(vp);
	int error;

	ILOCK(ip);
	error = iaccess(ip, mode, cr);
	IUNLOCK(ip);
	return error;
}

/* ARGSUSED */
STATIC int
s5lookup(dvp, nm, vpp, pnp, flags, rdir, cr)
	struct vnode *dvp;
	char *nm;
	struct vnode **vpp;
	struct pathname *pnp;
	int flags;
	struct vnode *rdir;
	struct cred *cr;
{
	register struct inode *ip;
	struct inode *xip;
	register int error;

	/*
	 * Null component name is synonym for directory being searched.
	 */
	if (*nm == '\0') {
		VN_HOLD(dvp);
		*vpp = dvp;
		return 0;
	}

	ip = VTOI(dvp);
	error = dirlook(ip, nm, &xip, cr);
	ITIMES(ip);
	if (error == 0) {
		ip = xip;
		*vpp = ITOV(ip);
		ITIMES(ip);
		IUNLOCK(ip);
		/*
		 * If vnode is a device return special vnode instead.
		 */
		if (ISVDEV((*vpp)->v_type)) {
			struct vnode *newvp;

			newvp =
			  specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
			VN_RELE(*vpp);
			if (newvp == NULL)
				error = ENOSYS;
			else
				*vpp = newvp;
		}
	}
	return error;
}

STATIC int
s5create(dvp, name, vap, excl, mode, vpp, cr)
	struct vnode *dvp;
	char *name;
	struct vattr *vap;
	enum vcexcl excl;
	int mode;
	struct vnode **vpp;
	struct cred *cr;
{
	register int error;
	register struct inode *ip = VTOI(dvp);
	struct inode *xip;

	xip = NULL;
	error = direnter(ip, name, DE_CREATE, (struct inode *) 0,
	  (struct inode *) 0, vap, &xip, cr);
	ITIMES(ip);
	ip = xip;

	/*
	 * If this is a mount point, traverse it and return.  There's
	 * nothing more we can do here because the resultant vnode may
	 * be of a different file system type.
	 */
	if (error == 0 && ITOV(ip)->v_vfsmountedhere) {
		vnode_t *vp = ITOV(ip);

		IUNLOCK(ip);
		if ((error = traverse(&vp)) == 0)
			*vpp = vp;
		return error;
	}

	/*
	 * If the file already exists and this is a non-exclusive create,
	 * check permissions and allow access for non-directories.
	 * Read-only create of an existing directory is also allowed.
	 * We fail an exclusive create of anything which already exists.
	 */
	if (error == EEXIST) {
		if (excl == NONEXCL) {
			if (((ip->i_mode & IFMT) == IFDIR) && (mode & IWRITE))
				error = EISDIR;
			else if (mode)
				error = iaccess(ip, mode, cr);
			else
				error = 0;
		}
		if (error)
			iput(ip);
		else if (((ip->i_mode & IFMT) == IFREG)
		  && (vap->va_mask & AT_SIZE) && vap->va_size == 0)
			/*
			 * Truncate regular files, if requested by caller.
			 */
			itrunc(ip);
	} 
	if (error)
		return error;
	*vpp = ITOV(ip);
	ITIMES(ip);
	if (((ip->i_mode & IFMT) == IFREG) && (vap->va_mask & AT_SIZE))
		error = fs_vcode(ITOV(ip), &ip->i_vcode);
	IUNLOCK(ip);
	/*
	 * If vnode is a device return special vnode instead.
	 */
	if (!error && ISVDEV((*vpp)->v_type)) {
		struct vnode *newvp;

		newvp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (newvp == NULL)
			error = ENOSYS;
		else
			*vpp = newvp;
	}

	return error;
}

STATIC int
s5remove(vp, nm, cr)
	struct vnode *vp;
	char *nm;
	struct cred *cr;
{
	register struct inode *ip = VTOI(vp);
	int error;

	error = dirremove(ip, nm, (struct inode *) 0, (struct vnode *) 0,
	  DR_REMOVE, cr);
	ITIMES(ip);
	return error;
}

/*
 * Link a file or a directory.  Only the super-user is allowed to make a
 * link to a directory.
 */
STATIC int
s5link(tdvp, svp, tnm, cr)
	register struct vnode *tdvp;
	struct vnode *svp;
	char *tnm;
	struct cred *cr;
{
	register struct inode *tdp, *sip;
	struct vnode *realvp;
	int error;

	if (VOP_REALVP(svp, &realvp) == 0)
		svp = realvp;
	sip = VTOI(svp);
	if (svp->v_type == VDIR && !suser(cr))
		return EPERM;
	tdp = VTOI(tdvp);
	error = direnter(tdp, tnm, DE_LINK, (struct inode *) 0,
	  sip, (struct vattr *) 0, (struct inode **) 0, cr);
	ITIMES(sip);
	ITIMES(tdp);
	return error;
}

/*
 * Rename a file or directory.
 * We are given the vnode and entry string of the source and the
 * vnode and entry string of the place we want to move the source
 * to (the target).  The essential operation is:
 *	unlink(target);
 *	link(source, target);
 *	unlink(source);
 * but "atomically".  Can't do full commit without saving state in
 * the inode on disk, which isn't feasible at this time.  Best we
 * can do is always guarantee that the TARGET exists.
 */
STATIC int
s5rename(sdvp, snm, tdvp, tnm, cr)
	struct vnode *sdvp;		/* old (source) parent vnode */
	char *snm;			/* old (source) entry name */
	struct vnode *tdvp;		/* new (target) parent vnode */
	char *tnm;			/* new (target) entry name */
	struct cred *cr;
{
	struct inode *sip;		/* source inode */
	register struct inode *sdp;	/* old (source) parent inode */
	register struct inode *tdp;	/* new (target) parent inode */
	register int error;

	sdp = VTOI(sdvp);
	tdp = VTOI(tdvp);
	/*
	 * Look up inode of file we're supposed to rename.
	 */
	if (error = dirlook(sdp, snm, &sip, cr))
		return error;
	IUNLOCK(sip);
	/*
	 * Make sure we can delete the source entry.  This requires
	 * write permission on the containing directory.  If that
	 * directory is "sticky" it further requires (except for the
	 * super-user) that the user own the directory or the source 
	 * entry, or else have permission to write the source entry.
	 */
	if (((error = iaccess(sdp, IWRITE, cr)) != 0)
	  || ((sdp->i_mode & ISVTX) && cr->cr_uid != 0
	    && cr->cr_uid != sdp->i_uid && cr->cr_uid != sip->i_uid
	    && ((error = iaccess(sip, IWRITE, cr)) != 0))) {
	    ILOCK(sip);
	    iput(sip);
		return error;
	  }
	/*
	 * Check for renaming '.' or '..' or alias of '.'
	 */
	if (strcmp(snm, ".") == 0 || strcmp(snm, "..") == 0 || sdp == sip) {
		error = EINVAL;
		goto out;
	}
	/*
	 * Link source to the target.
	 */
	if (error = direnter(tdp, tnm, DE_RENAME, sdp, sip,
	  (struct vattr *) 0, (struct inode **) 0, cr))
		goto out;
	/*
	 * Remove the source entry.  dirremove() checks that the entry
	 * still reflects sip, and returns an error if it doesn't.
	 * If the entry has changed just forget about it.  Release
	 * the source inode.
	 */
	if ((error = dirremove(sdp, snm, sip, (struct vnode *) 0,
	  DR_RENAME, cr)) == ENOENT)
		error = 0;

out:
	/*
	 * Check for special error return which indicates a no-op
	 * rename.
	 */
	if (error == ESAME)
		error = 0;
	ITIMES(sdp);
	ITIMES(tdp);
	ILOCK(sip);
	iput(sip);
	return error;
}

STATIC int
s5mkdir(dvp, dirname, vap, vpp, cr)
	struct vnode *dvp;
	char *dirname;
	struct vattr *vap;
	struct vnode **vpp;
	struct cred *cr;
{
	register struct inode *ip;
	struct inode *xip;
	register int error;

	ASSERT((vap->va_mask & (AT_TYPE|AT_MODE)) == AT_TYPE|AT_MODE);

	ip = VTOI(dvp);
	error = direnter(ip, dirname, DE_MKDIR, (struct inode *) 0,
	  (struct inode *) 0, vap, &xip, cr);
	ITIMES(ip);
	if (error == 0) {
		ip = xip;
		*vpp = ITOV(ip);
		ITIMES(ip);
		IUNLOCK(ip);
	} else if (error == EEXIST)
		iput(xip);
	return error;
}

STATIC int
s5rmdir(vp, nm, cdir, cr)
	struct vnode *vp;
	char *nm;
	struct vnode *cdir;
	struct cred *cr;
{
	register struct inode *ip = VTOI(vp);
	int error;

	error = dirremove(ip, nm, (struct inode *) 0, cdir, DR_RMDIR, cr);
	ITIMES(ip);
	return error;
}

#define	DIRBUFSIZE	1048
STATIC void filldir();
caddr_t *dirbufp;

/* ARGSUSED */
STATIC int
s5readdir(vp, uiop, cr, eofp)
	struct vnode *vp;
	register struct uio *uiop;
	struct cred *cr;
	int *eofp;
{
	struct fbuf *fbp = NULL;
	struct inode *ip = VTOI(vp);
	struct s5vfs *s5vfsp = S5VFS(vp->v_vfsp);
	int bsize, bmask, error = 0, ran_out = 0;
	off_t diroff = uiop->uio_offset;
	int oresid = uiop->uio_resid;
	caddr_t direntp;

	/*
	 * The upper level brackets the application of this operation
	 * with an RWLOCK/RWUNLOCK pair, in imitation of a read(2) on a
	 * directory.  (Though note that Sun locks a read of a regular
	 * regular file but not of a directory.)
	 */

	/*
	 * Error if not on directory entry boundary.
	 */
	if (diroff % SDSIZ != 0)
		return ENOENT;
	if (diroff < 0)
		return EINVAL;
	bsize = VBSIZE(vp);
	bmask = s5vfsp->vfs_bmask;
	/*
	 * Allocate space to hold dirent structures.  Use the most common
	 * request size (1048).
	 */
	direntp = kmem_fast_alloc((caddr_t *)&dirbufp, DIRBUFSIZE, 1, KM_SLEEP);
	/*
	 * In a loop, read successive blocks of the directory,
	 * converting the entries to fs-independent form and
	 * copying out, until the end of the directory is
	 * reached or the caller's request is satisfied.
	 */
	do {
		int blkoff, leftinfile, leftinblock;
		char *directp;

		/*
		 * If at or beyond end of directory, we're done.
		 */
		if ((leftinfile = ip->i_size - diroff) <= 0)
			break;
		/*
		 * Map in next block of directory entries.
		 */
		if (error = fbread(vp, diroff & ~bmask, bsize, S_OTHER, &fbp))
			goto out;
		ip->i_flag |= IACC;
		blkoff = diroff & bmask;	/* offset in block */
		leftinblock = MIN(bsize-blkoff, leftinfile);
		directp = fbp->fb_addr + blkoff;
		/*
		 * In a loop, fill the allocated space with fs-independent
		 * directory structures and copy out, until the current
		 * disk block is exhausted or the caller's request has
		 * been satisfied.
		 */
		do {
			int ndirent;	/* nbytes of "dirent" filled */
			int ndirect;	/* nbytes of "direct" consumed */
			int maxndirent;	/* max nbytes of "dirent" to fill */

			maxndirent = MIN(DIRBUFSIZE, uiop->uio_resid);
			filldir(direntp, maxndirent, directp, leftinblock,
			  diroff, &ndirent, &ndirect);
			directp += ndirect;
			leftinblock -= ndirect;
			diroff += ndirect;
			if (ndirent == -1) {
				ran_out = 1;
				goto out;
			} else if (ndirent == 0)
				break;
			if (error = uiomove(direntp, ndirent, UIO_READ, uiop))
				goto out;
		} while (leftinblock > 0 && uiop->uio_resid > 0);
		fbrelse(fbp, S_OTHER);
		fbp = NULL;
	} while (uiop->uio_resid > 0 && diroff < ip->i_size);
out:
	/*
	 * If we ran out of room but haven't returned any entries, error.
	 */
	if (ran_out && uiop->uio_resid == oresid)
		error = EINVAL;
	if (fbp)
		fbrelse(fbp, S_OTHER);
	kmem_fast_free((caddr_t *)&dirbufp, direntp);
	/*
	 * Offset returned must reflect the position in the directory itself,
	 * independent of how much fs-independent data was returned.
	 */
	if (error == 0) {
		uiop->uio_offset = diroff;
		if (eofp)
			*eofp = (diroff >= ip->i_size);
	}
	ITIMES(ip);
	return error;
}

/*
 * Convert fs-specific directory entries ("struct direct") to
 * fs-independent form ("struct dirent") in a supplied buffer.
 * Returns, through reference parameters, the number of bytes of
 * "struct dirent" with which the buffer was filled and the number of
 * bytes of "struct direct" which were consumed.  If there was a
 * directory entry to convert but no room to hold it, the "number
 * of bytes filled" will be -1.
 */
STATIC void
filldir(direntp, nmax, directp, nleft, diroff, ndirentp, ndirectp)
	char	*direntp;	/* buffer to be filled */
	int	nmax;		/* max nbytes to be filled */
	char	*directp;	/* buffer of disk directory entries */
	int	nleft;		/* nbytes of dir entries left in block */
	off_t	diroff;		/* offset in directory */
	int	*ndirentp;	/* nbytes of "struct dirent" filled */
	int	*ndirectp;	/* nbytes of "struct direct" consumed */
{
	int ndirent = 0, ndirect = 0, namelen, reclen;
	register struct direct *olddirp;
	register struct dirent *newdirp = (struct dirent *) direntp;
	int direntsz = (char *) newdirp->d_name - (char *) newdirp;
	long int ino;

	for (olddirp = (struct direct *) directp;
	  olddirp < (struct direct *) (directp + nleft);
	  olddirp++, ndirect += SDSIZ) {
		if ((ino = olddirp->d_ino) == 0)
			continue;
		namelen = (olddirp->d_name[DIRSIZ-1] == '\0') ?
		  strlen(olddirp->d_name) : DIRSIZ;
		reclen = (direntsz + namelen + 1 + (NBPW-1)) & ~(NBPW-1);
		if (ndirent + reclen > nmax) {
			if (ndirent == 0)
				ndirent = -1;
			break;
		}
		ndirent += reclen;
		newdirp->d_reclen = (short)reclen;
		newdirp->d_ino = ino;
		newdirp->d_off = diroff + ndirect + SDSIZ;
		bcopy(olddirp->d_name, newdirp->d_name, namelen);
		newdirp->d_name[namelen] = '\0';
		newdirp = (struct dirent *) (((char *) newdirp) + reclen);
	}
	*ndirentp = ndirent;
	*ndirectp = ndirect;
}

STATIC int
s5symlink(dvp, linkname, vap, target, cr)
	register struct vnode *dvp;	/* ptr to parent dir vnode */
	char *linkname;			/* name of symbolic link */
	struct vattr *vap;		/* attributes */
	char *target;			/* target path */
	struct cred *cr;		/* user credentials */
{
	struct inode *ip, *dip = VTOI(dvp);
	int error;

	if ((error = direnter(dip, linkname, DE_CREATE, (struct inode *) 0,
	  (struct inode *) 0, vap, &ip, cr)) == 0) {
		error = rdwri(UIO_WRITE, ip, target, strlen(target),
		  (off_t) 0, UIO_SYSSPACE, IO_SYNC, (int *) 0);
		iput(ip);
	} else if (error == EEXIST)
		iput(ip);
	ITIMES(dip);
	return error;
}

/* ARGSUSED */
STATIC int
s5readlink(vp, uiop, cr)
	struct vnode *vp;
	register struct uio *uiop;
	struct cred *cr;
{
	register struct inode *ip;
	register int error;

	if (vp->v_type != VLNK)
		return EINVAL;
	ip = VTOI(vp);
	ILOCK(ip);
	error = readi(ip, uiop, 0);
	ITIMES(ip);
	IUNLOCK(ip);
	return error;
}

/* ARGSUSED */
STATIC int
s5fsync(vp, cr)
	struct vnode *vp;
	struct cred *cr;
{
	register struct inode *ip = VTOI(vp);
	register int error;

	IRWLOCK(ip);
	ILOCK(ip);
	error = syncip(ip, 0);		/* Do synchronous writes */
	ITIMES(ip);
	IUNLOCK(ip);
	IRWUNLOCK(ip);
	return error;
}

STATIC void
s5inactive(vp, cr)
	register struct vnode *vp;
	struct cred *cr;
{
	ASSERT(vp->v_count == 0);
	if (vp->v_type == VCHR)
		vp->v_stream = NULL;
	iinactive(VTOI(vp), cr);
}

STATIC int
s5fid(vp, fidpp)
	struct vnode *vp;
	struct fid **fidpp;
{
	register struct ufid *ufid;

	ufid = (struct ufid *) kmem_zalloc(sizeof(struct ufid), KM_SLEEP);
	ufid->ufid_len = sizeof(struct ufid) - sizeof(u_short);
	ufid->ufid_ino = VTOI(vp)->i_number;
	ufid->ufid_gen = VTOI(vp)->i_gen;
	*fidpp = (struct fid *) ufid;
	return 0;
}

STATIC void
s5rwlock(vp)
	struct vnode *vp;
{
	IRWLOCK(VTOI(vp));
}

STATIC void
s5rwunlock(vp)
	struct vnode *vp;
{
	IRWUNLOCK(VTOI(vp));
}

/* ARGSUSED */
STATIC int
s5seek(vp, ooff, noffp)
	struct vnode *vp;
	off_t ooff;
	off_t *noffp;
{
	return *noffp < 0 ? EINVAL : 0;
}

/* ARGSUSED */
STATIC int
s5frlock(vp, cmd, bfp, flag, offset, cr)
	register struct vnode *vp;
	int cmd;
	struct flock *bfp;
	int flag;
	off_t offset;
	cred_t *cr;
{
	register struct inode *ip = VTOI(vp);

	/*
	 * If file is being mapped, disallow frlock.
	 */
	if (ip->i_mapcnt > 0)
		return EAGAIN;

	return fs_frlock(vp, cmd, bfp, flag, offset, cr);
}

/* ARGSUSED */
STATIC int
s5space(vp, cmd, bfp, flag, offset, cr)
	struct vnode *vp;
	int cmd;
	struct flock *bfp;
	int flag;
	off_t offset;
	struct cred *cr;
{
	int error;

	if (cmd != F_FREESP)
		return EINVAL;
	if ((error = convoff(vp, bfp, 0, offset)) == 0)
		error = s5freesp(vp, bfp, flag);
	return error;
}

/*
 * Count total number of blocks allocated to the file.  Call s5bloop()
 * to count indirect blocks.
 */
STATIC int
s5getsp(vp, blkcntp)
	register struct vnode *vp;
	register u_long *blkcntp;
{
	register int i;
	vtype_t type;
	register struct inode *ip;
	register struct vfs *vfsp;
	register daddr_t bn;
	int error;
	
	*blkcntp = 0;

	/*
	 * There are no file blocks allocated except for the types
	 * shown below.  In particular special files (VCHR and VBLK)
	 * have no block allocation.
	 */
	type = vp->v_type;
	if (type != VREG && type != VDIR && type != VLNK)
		return 0;

	error = 0;
	ip = VTOI(vp);
	vfsp = vp->v_vfsp;

	for (i = NADDR - 1; i >= 0; i--) {
		if ((bn = ip->i_addr[i]) == 0)
			continue;

		switch (i) {

		default:
			(*blkcntp)++;
			break;

		case NADDR-3:
			if (error = s5bloop(vfsp, bn, 0, 0, blkcntp))
				goto out;
			break;

		case NADDR-2:
			if (error = s5bloop(vfsp, bn, 1, 0, blkcntp))
				goto out;
			break;

		case NADDR-1:
			if (error = s5bloop(vfsp, bn, 1, 1, blkcntp))
				goto out;
			break;
		}
	}
out:
	return error;
}

/*
 * This routine counts indirect blocks.  It will be called recursively
 * if there is more than one level of indirection.
 */
STATIC int
s5bloop(vfsp, bn, f1, f2, blkcntp)
	register struct vfs *vfsp;
	daddr_t bn;
	int f1;
	int f2;
	register u_long *blkcntp;
{
	dev_t dev;
	register i;
	register struct buf *bp;
	register struct s5vfs *s5vfsp = S5VFS(vfsp);
	register daddr_t *bap;
	register daddr_t nb;
	int error = 0;

	dev = vfsp->vfs_dev;
	bp = NULL;
	for (i = s5vfsp->vfs_nindir - 1; i >= 0; i--) {
		if (bp == NULL) {
			bp = bread(dev, bn, vfsp->vfs_bsize);
			if ((error = geterror(bp)) != 0) {
				brelse(bp);
				return error;
			}
			bap = bp->b_un.b_daddr;
		}
		if ((nb = bap[i]) == 0)
			continue;
		if (f1) {
			brelse(bp);
			bp = NULL;
			if (error = s5bloop(vfsp, nb, f2, 0, blkcntp))
				break;
		} else
			(*blkcntp)++;
	}
	if (bp != NULL)
		brelse(bp);
	(*blkcntp)++;
	return error;
}


/*
 * Page-handling operations: getpage() and putpage().
 */

#define LTOPBLK(blkno, bsize)	(blkno * ((bsize>>SCTRSHFT)))
#define S5MINBSIZE	512
/* 
 * The extra entry is for read-ahead support.
*/
#define S5MAXREQ	MAX(PAGESIZE/S5MINBSIZE, 2)

int s5_ra = 1;
int s5_lostpage;	/* number of times we lost original page */

/*
 * Called from pvn_getpages() or s5getpage() to get a particular page.
 * When we are called the inode is already locked.
 *
 * If rw == S_WRITE and block is not allocated, need to alloc block.
 * If ppp == NULL, async I/O is requested.
 */
/* ARGSUSED */
STATIC int
s5getapage(vp, off, protp, pl, plsz, seg, addr, rw, cr)
	struct vnode *vp;
	register u_int off;
	u_int *protp;
	struct page *pl[];
	u_int plsz;
	struct seg *seg;
	addr_t addr;
	enum seg_rw rw;
	struct cred *cr;
{
	register struct inode *ip;
	register struct s5vfs *s5vfsp = S5VFS(vp->v_vfsp);
	register int bsize;
	u_int xlen;
	int bn[S5MAXREQ], *bnp, *bnp2;
	struct buf *bp[S5MAXREQ], **bufp;
	struct vnode *devvp;
	struct page *pp, *pp2, **ppp, *pagefound;
	daddr_t lbn;
	u_int io_off, io_len;
	u_int lbnoff, curoff;
	int err = 0, nio, pgoff;
	dev_t dev;
	int pgaddr;
	int multi_io, i, do_ra, blksz;

	ip = VTOI(vp);
	bsize = VBSIZE(vp);
	dev = ip->i_dev;
	multi_io = PAGESIZE > bsize ? PAGESIZE/bsize : 1;

reread:
	do_ra = 0;
	pagefound = NULL;
	pgoff = 0;
	lbn = off >> s5vfsp->vfs_bshift;
	curoff = lbnoff = off & ~(bsize - 1);

	if (pl != NULL)
		pl[0] = NULL;

	if ((pagefound = page_find(vp, off)) != NULL)
		goto pagefound_out;

	bnp = bn;
	bufp = bp;
	for (i = 0; i < S5MAXREQ; i++, bnp++, bufp++) {
		*bnp = S5_HOLE;
		*bufp = NULL;
	}

	bnp = bn;
	nio = 0; 

	if (multi_io > 1) {
		for(i=0; i < multi_io; i=i+2) {
			bnp2 = bnp + 1;
			err = bmap(ip, lbn+i, bnp, bnp2, rw, 0);
			if (err)
				goto out;
			if((ip->i_size > curoff + bsize) && (*bnp2 == S5_HOLE)) {
					err = bmap(ip, lbn+i+1, bnp2, 0, rw, 0);
					if (err)
						goto out;
			}
			if (protp != NULL) {
				if ((*bnp == S5_HOLE) || ((*bnp2 == S5_HOLE) &&
			  	(curoff+bsize < ip->i_size)))
					*protp &= ~PROT_WRITE;
			}
			if (*bnp != S5_HOLE)
				nio++;
			if ((*bnp2 != S5_HOLE) && ((curoff + bsize) < ip->i_size))
				nio++;
			bnp = bnp + 2;
			curoff = curoff + 2*bsize;
		}
	} else {
		bnp2 = bnp + 1;
		err = bmap(ip, lbn, bnp, bnp2, rw, 0);
		if (err)
			goto out;
		nio = 1;
		if (s5_ra && ip->i_nextr == off && *bnp2 != S5_HOLE &&
	  	    lbnoff + bsize < ip->i_size) {
			do_ra = 1;
		} else {
			do_ra = 0;
		}
	}

	bnp = bn;
	bufp = bp;
	if (*bnp == S5_HOLE && protp != NULL)
		*protp &= ~PROT_WRITE;

	if (nio == 0)
		nio = 1;

	devvp = S5VFS(vp->v_vfsp)->vfs_devvp;

	/*
	 * Although the previous page_find() failed to find the page,
	 * we have to check again since it may have entered the cache
	 * during or after the calls to bmap().
	 */
	if ((pagefound = page_find(vp, off)) != NULL)
		goto pagefound_out;

	if (ip->i_size > lbnoff)
		blksz = MIN((roundup(ip->i_size, PAGESIZE) - lbnoff), bsize);
	else
		blksz = bsize;

	if (*bnp == S5_HOLE || off >= lbnoff + blksz) {
		/*
		 * Block for this page is not allocated or the offset
		 * is beyond the current allocation size (from bmap)
		 * and the page was not found.  If we need a page,
		 * allocate and return a zero page.
		 */
		if (pl != NULL) {
			pp = rm_allocpage(seg, addr, PAGESIZE, P_CANWAIT);
			for (i=0; i<multi_io; i++)
				pp->p_dblist[i] = bn[i];
			if (page_enter(pp, vp, off))
				cmn_err(CE_PANIC,
				  "s5_getapage page_enter");
			pagezero(pp, 0, PAGESIZE);
			page_unlock(pp);
			pp->p_nio = nio;
			if (nio > 1) {
				pp->p_intrans = pp->p_pagein = 1;
				PAGE_HOLD(pp);
			}
			pl[0] = pp;
			pl[1] = NULL;
		}
	} else {
		/*
		 * Need to really do disk I/O to get the page(s).
		 */
		pp = pvn_kluster(vp, off, seg, addr, &io_off, &io_len,
		  lbnoff, blksz, 0);

		ASSERT(pp != NULL);
		pp2 = pp;
		do {
			for (i=0; i<multi_io; i++)
				pp2->p_dblist[i] = bn[i];
			pp2 = pp2->p_next;
		} while (pp2 != pp);

		if (pl != NULL) {
			register int sz;

			if (plsz >= io_len) {
				/*
				 * Everything fits, set up to load
				 * up and hold all the pages.
				 */
				pp2 = pp;
				sz = io_len;
			} else {
				/*
				 * Set up to load plsz worth
				 * starting at the needed page.
				 */
				for (pp2 = pp; pp2->p_offset != off;
				  pp2 = pp2->p_next) {
					ASSERT(pp2->p_next->p_offset !=
					    pp->p_offset);
				}
				sz = plsz;
			}

			ppp = pl;
			do {
				PAGE_HOLD(pp2);
				*ppp++ = pp2;
				pp2 = pp2->p_next;
				sz -= PAGESIZE;
			} while (sz > 0);
			*ppp = NULL;		/* terminate list */
		}

		if (nio > 1)
			pp->p_nio = nio;

		bp[0] = pageio_setup(pp, io_len, devvp,
		  pl == NULL ? (B_ASYNC | B_READ) : B_READ);

		bp[0]->b_edev = dev;
		bp[0]->b_dev = cmpdev(dev);
		bp[0]->b_blkno = LTOPBLK(*bnp, bsize) + 
				   ((io_off - lbnoff) >> SCTRSHFT);
#if 0 /* if smart driver */
		bp[0]->b_un.b_addr = 0;
#else
		pgaddr = pfntokv(page_pptonum(pp));
		bp[0]->b_un.b_addr = (caddr_t)pgaddr;
#endif

		/*
		 * Zero part of page which we are not
		 * going to be reading from disk now.
		 */
		xlen = io_len & PAGEOFFSET;
		if (xlen != 0)
			pagezero(pp->p_prev, xlen, PAGESIZE - xlen);

		(*bdevsw[getmajor(dev)].d_strategy)(bp[0]);
		ip->i_nextr = io_off + io_len;
		vminfo.v_pgin++;
		vminfo.v_pgpgin += btopr(io_len);

	}

	bnp++;
	bufp++;
	for (i = 1; i < multi_io; i++, bnp++, bufp++) {
		lbnoff += bsize;

		if (*bnp != S5_HOLE && lbnoff < ip->i_size) {
			addr_t addr2;

			addr2 = addr + (lbnoff - off);

			/*
			 * We only need to increment keepcnt once,
			 * because only when p_nio is less than 1
			 * will PAGE_RELE() be called.
			 */
			pp2 = pp;
			if (nio < 2) {
				PAGE_HOLD(pp2);
				pp2->p_intrans = 1;
				pp2->p_pagein = 1;
			}
			io_len = bsize;
			pgoff = i * bsize;

			if (pp2 != NULL) {
				*bufp = pageio_setup(pp2, io_len, devvp,
			    	  pl == NULL ?
				    (B_ASYNC | B_READ) : B_READ);

				(*bufp)->b_edev = dev;
				(*bufp)->b_dev = cmpdev(dev);
				(*bufp)->b_blkno = LTOPBLK(*bnp, bsize);
#if 0 /* if smart driver */
				(*bufp)->b_un.b_addr = (caddr_t)pgoff;
#else
				pgaddr = pfntokv(page_pptonum(pp2));
				(*bufp)->b_un.b_addr = (caddr_t)(pgaddr + pgoff);
#endif

				/*
			 	 * Zero part of page which we are not
			 	 * going to be reading from disk now.
			 	 */
				xlen = (io_len + pgoff) & PAGEOFFSET;
				if (xlen != 0)
					pagezero(pp2->p_prev, xlen,
					  PAGESIZE - xlen);
				(*bdevsw[getmajor(dev)].d_strategy)(*bufp);
				vminfo.v_pgin++;
				vminfo.v_pgpgin += btopr(io_len);
			}
		}
	}

	if (do_ra) {
		addr_t addr2;

		lbnoff += bsize;
		addr2 = addr + (lbnoff - off);
		if (addr2 >= seg->s_base + seg->s_size) {
			pp2 = NULL;
		} else {
			pp2 = pvn_kluster(vp, lbnoff, seg, addr2, &io_off,
				&io_len, lbnoff, blksz, 1);
		}
		pgoff = 0;

		if (pp2 != NULL) {
			pp = pp2;
			pp->p_dblist[0] = bn[1];

			*bufp = pageio_setup(pp2, io_len, devvp, (B_ASYNC | B_READ));
			(*bufp)->b_edev = dev;
			(*bufp)->b_dev = cmpdev(dev);
			(*bufp)->b_blkno = LTOPBLK(*bnp, bsize) + ((io_off - lbnoff) >> SCTRSHFT);
			pgaddr = pfntokv(page_pptonum(pp2));
			(*bufp)->b_un.b_addr = (caddr_t)pgaddr;
			xlen = (io_len + pgoff) & PAGEOFFSET;
			if (xlen != 0)
				pagezero(pp2->p_prev, xlen,
					 PAGESIZE - xlen);
			(*bdevsw[getmajor(dev)].d_strategy)(*bufp);
			vminfo.v_pgin++;
			vminfo.v_pgpgin += btopr(io_len);
		}
	}
out:
	if (pl == NULL)
		return err;

	bufp = bp;
	for (i = 0; i < multi_io; i++, bufp++) {
		if (*bufp != NULL) {
			if (err == 0)
				err = biowait(*bufp);
			else
				(void) biowait(*bufp);
			pageio_done(*bufp);
		}
	}

pagefound_out:
	if (pagefound != NULL) {
		int s;

		/*
		 * We need to be careful here because if the page was
		 * previously on the free list, we might have already
		 * lost it at interrupt level.
		 */
		s = splvm();
		if (pagefound->p_vnode == vp && pagefound->p_offset == off) {
			/*
			 * If the page is still intransit or if
			 * it is on the free list call page_lookup
			 * to try and wait for / reclaim the page.
			 */
			if (pagefound->p_intrans || pagefound->p_free)
				pagefound = page_lookup(vp, off);
		}
		(void) splx(s);
		if (pagefound == NULL || pagefound->p_offset != off ||
		    pagefound->p_vnode != vp || pagefound->p_gone) {
			s5_lostpage++;
			goto reread;
		}
		if (pl != NULL) {
			PAGE_HOLD(pagefound);
			pl[0] = pagefound;
			pl[1] = NULL;
		}
	}

	if (err && pl != NULL) {
		for (ppp = pl; *ppp != NULL; *ppp++ = NULL)
			PAGE_RELE(*ppp);
	}

	return err;
}

/*
 * Return all the pages from [off..off+len) in given file
 */
STATIC int
s5getpage(vp, off, len, protp, pl, plsz, seg, addr, rw, cr)
	struct vnode *vp;
	u_int off;
	u_int len;
	u_int *protp;
	struct page *pl[];
	u_int plsz;
	struct seg *seg;
	addr_t addr;
	enum seg_rw rw;
	struct cred *cr;
{
	struct inode *ip = VTOI(vp);
	int err;

	if (vp->v_flag & VNOMAP)
		return ENOSYS;

	/*
	 * We may be getting called as a side effect of a bmap using
	 * fbread() when the blocks might be being allocated and the
	 * size has not yet been up'ed.  In this case we want to be
	 * able to return zero pages if we get back S5_HOLE from
	 * calling bmap for a non write case here.
	 */
	if (off + len > ip->i_size + PAGEOFFSET && seg != segkmap)
		return EFAULT;	/* beyond EOF */

	if (protp != NULL)
		*protp = PROT_ALL;

	ILOCK(ip);

	if (len <= PAGESIZE)
		err = s5getapage(vp, off, protp, pl, plsz, seg, addr, rw, cr);
	else
		err = pvn_getpages(s5getapage, vp, off, len, protp, pl, plsz,
		  seg, addr, rw, cr);

	/*
	 * If the inode is not already marked for IACC (in rwip() for read)
	 * and the inode is not marked for no access time update (in rwip()
	 * for write) then update the inode access time and mod time now.
	 */
	if ((ip->i_flag & (IACC|INOACC)) == 0) {
		if (rw != S_OTHER)
			ip->i_flag |= IACC;
		if (rw == S_WRITE)
			ip->i_flag |= IUPD;
		ITIMES(ip);
	}

	IUNLOCK(ip);

	return err;
}

/*
 * Flags are composed of {B_ASYNC, B_INVAL, B_FREE, B_DONTNEED, B_FORCE}
 * If len == 0, do from off to EOF.
 *
 * The normal cases should be len == 0 & off == 0 (entire vp list),
 * len == MAXBSIZE (from segmap_release actions), and len == PAGESIZE
 * (from pageout).
 *
 */
STATIC int
s5putpage(vp, off, len, flags, cr)
	register struct vnode *vp;
	u_int off, len;
	int flags;
	struct cred *cr;
{
	register struct inode *ip;
	register struct page *pp;
	struct page *dirty, *io_list;
	register u_int io_off, io_len;
	daddr_t lbn;
	int bn[S5MAXREQ], *bnp, *bnp2;
	u_int lbn_off;
	int bsize;
	int multi_io;
	int vpcount;
	int err = 0, werr;
	struct s5vfs *s5vfsp = S5VFS(vp->v_vfsp);
	int fs_bshift, fs_bmask;
	int i, nio, curoff;

	if (vp->v_flag & VNOMAP)
		return ENOSYS;

	ip = VTOI(vp);
	/*
	 * If it's the pageout daemon or the swapper and the inode is already
	 * locked by someone else, don't sleep.
	 */
	if ((ip->i_flag & ILOCKED) 
	  && (curproc->p_pid == 0 || curproc->p_pid == 2))
		return EAGAIN;

	bsize = VBSIZE(vp);
	fs_bshift = s5vfsp->vfs_bshift;
	fs_bmask = ~s5vfsp->vfs_bmask;

	if (vp->v_pages == NULL || off >= ip->i_size)
		return 0;

	multi_io = PAGESIZE > bsize ? PAGESIZE/bsize : 1;
	vpcount = vp->v_count;
	VN_HOLD(vp);

again:
	if (len == 0) {
		/*
		 * Search the entire vp list for pages >= off
		 */
		ILOCK(ip);
		dirty = pvn_vplist_dirty(vp, off, flags);
		IUNLOCK(ip);
	} else {
		/*
		 * Do a range from [off...off + len) via page_find.
		 * We set limits so that we kluster to bsize boundaries.
		 */
		if (off >= ip->i_size)
			dirty = NULL;
		else {
			u_int fsize, eoff;

			/*
			 * Use MAXBSIZE rounding to get indirect block pages
			 * which might beyond roundup(ip->i_size, PAGESIZE);
			 */
			fsize = (ip->i_size + bsize - 1) & fs_bmask;
			eoff = MIN(off + len, fsize);
			dirty = pvn_range_dirty(vp, off, eoff,
			  (u_int)(off & fs_bmask),
			  (u_int)((eoff + bsize - 1) & fs_bmask),
			  flags);
		}
	}

	/*
	 * Now pp will have the list of kept dirty pages marked for
	 * write back.  All the pages on the pp list need to still
	 * be dealt with here.  Verify that we can really can do the
	 * write back to the filesystem and if not and we have some
	 * dirty pages, return an error condition.
	 */
	if ((vp->v_vfsp->vfs_flag & VFS_RDONLY) && dirty != NULL)
		err = EROFS;
	else
		err = 0;

	if (dirty != NULL) {
		/*
		 * Destroy the read-ahead value now since we are
		 * really going to write.
		 */
		ip->i_nextr = 0;

		/*
		 * If the modified time on the inode has not already been
		 * set elsewhere (e.g. for write/setattr) and this is not
		 * a call from msync (B_FORCE) we set the time now.
		 * This gives us approximate modified times for mmap'ed files
		 * which are modified via stores in the user address space.
		 */
		if ((ip->i_flag & IMODTIME) == 0 || (flags & B_FORCE)) {
			ip->i_flag |= IUPD;
			ITIMES(ip);
		}
		/*
		 * This is an attempt to clean up loose ends left by
		 * applications that store into mapped files.  It's
		 * insufficient, strictly speaking, for ill-behaved
		 * applications, but about the best we can do.
		 */
		if (vp->v_type == VREG) {
			ILOCK(ip);
			err = fs_vcode(vp, &ip->i_vcode);
			IUNLOCK(ip);
		}
	}

	/*
	 * Handle all the dirty pages not yet dealt with.
	 */
	bnp = bn;
	while (err == 0 && (pp = dirty) != NULL) {
		/*
		 * Pull off a contiguous chunk that fits in one lbn.
		 */
		curoff = io_off = pp->p_offset;
		lbn = io_off >> s5vfsp->vfs_bshift;
		nio = 0;

		for (i = 0, bnp = bn; i < multi_io; i++, bnp++) {
			*bnp = pp->p_dblist[i];
			if (*bnp != S5_HOLE)
				nio++;
		}

		page_sub(&dirty, pp);
		io_list = pp;
		io_len = PAGESIZE;
		lbn_off = lbn << s5vfsp->vfs_bshift;
	
		while (dirty != NULL && dirty->p_offset < lbn_off + bsize &&
		   dirty->p_offset == io_off + io_len) {
			pp = dirty;
			page_sub(&dirty, pp);
			/* 
			 * Add the page to the end of the list.  page_sortadd
			 * can do this without walking the list.
			 */
			page_sortadd(&io_list, pp);
			io_len += PAGESIZE;
		}

		/* 
		 * Check for page length rounding problems
	  	 */
		if (io_off + io_len > lbn_off + bsize) {
			ASSERT((io_off + io_len) - (lbn_off + bsize) < PAGESIZE);
			io_len = lbn_off + bsize - io_off;
		}

		bnp = bn;
		ASSERT(bsize < PAGESIZE || *bnp != S5_HOLE);

		/*
		 * I/O may be asynch, so need to set nio first.
		 */
		if (bsize < PAGESIZE && ip->i_size > lbn_off + bsize)
			pp->p_nio = nio;
		else
			pp->p_nio = 0;

		for (i = 0, bnp = bn; i < multi_io; i++, bnp++) {
			if (*bnp != S5_HOLE) {
				*bnp = LTOPBLK(*bnp, bsize);
				if (multi_io == 1) 
				 	werr = s5writelbn(ip, *bnp, io_list,
						  io_len, 0, flags);
				else
					werr = s5writelbn(ip, *bnp, io_list, bsize,
				  	   	  bsize*i, flags);
				if (err == 0)
					err = werr;
			}
		}
	}

	if (err && dirty != NULL)
		pvn_fail(dirty, B_WRITE|flags);
	else if ((flags & (B_INVAL|B_ASYNC)) == B_INVAL && len == 0
	  && off == 0 && vp->v_pages != NULL)
		goto again;

out:
	/*
	 * If we have just sync'ed back all the pages on
	 * the inode, we can turn off the IMODTIME flag.
	 */
	if (off == 0 && (len == 0 || len >= ip->i_size))
		ip->i_flag &= ~IMODTIME;

	/*
	 * Instead of using VN_RELE here we are careful to
	 * call the inactive routine only if the vnode
	 * reference count is now zero but wasn't zero
	 * coming into putpage.  This is to prevent calling
	 * the inactive routine on a vnode that is already
	 * considered to be in the "inactive" state.
	 */
	if (--vp->v_count == 0 && vpcount > 0)
		iinactive(ip, cr);
	return err;
}

/*
 * Flags are composed of {B_ASYNC, B_INVAL, B_FREE, B_DONTNEED}
 */
STATIC int
s5writelbn(ip, bn, pp, len, pgoff, flags)
	register struct inode *ip;
	daddr_t bn;
	struct page *pp;
	u_int len;
	u_int pgoff;
	int flags;
{
	struct buf *bp;
	int err;
	struct vnode *devvp;
	int bsize;

	bsize = VBSIZE(ITOV(ip));
	devvp = S5VFS(ITOV(ip)->v_vfsp)->vfs_devvp;
	if ((bp = pageio_setup(pp, len, devvp, B_WRITE | flags)) == NULL) {
		pvn_fail(pp, B_WRITE | flags);
		return ENOMEM;
	}

	bp->b_edev = ip->i_dev;
	bp->b_dev = cmpdev(ip->i_dev);
	bp->b_blkno = bn + ((pp->p_offset & (bsize - 1)) >> SCTRSHFT);
#if 0
	bp->b_un.b_addr = (caddr_t)pgoff;
#else
	bp->b_un.b_addr = (caddr_t)pfntokv(page_pptonum(pp)) + pgoff;
#endif

	(*bdevsw[getmajor(ip->i_dev)].d_strategy)(bp);

	/*
	 * If async, assume that pvn_done will handle the pages
	 * when I/O is done.
	 */
	if (flags & B_ASYNC)
		return 0;

	err = biowait(bp);
	pageio_done(bp);

	return err;
}


/* ARGSUSED */
STATIC int
s5map(vp, off, as, addrp, len, prot, maxprot, flags, cr)
	struct vnode *vp;
	u_int off;
	struct as *as;
	caddr_t *addrp;
	u_int len;
	u_int prot, maxprot;
	u_int flags;
	struct cred *cr;
{
	struct segvn_crargs vn_a;
	register int error = 0;
	register struct inode *ip;

	if (vp->v_flag & VNOMAP)
		return ENOSYS;

	if ((int)off < 0 || (int)(off + len) < 0)
		return EINVAL;

	if (vp->v_type != VREG)
		return ENODEV;

	/*
	 * If file is being locked, disallow mmap.
	 */
	if (vp->v_filocks != NULL)
		return EAGAIN;

	/*
	 * Don't allow a mapping beyond the last page in the file.
	 */
	if (off + len > ((VTOI(vp)->i_size + PAGEOFFSET) & PAGEMASK))
		return ENXIO;

	if ((flags & MAP_FIXED) == 0) {
		map_addr(addrp, len, (off_t)off, 1);
		if (*addrp == NULL)
			return ENOMEM;
	} else {
		/*
		 * User specified address - blow away any previous mappings
		 */
		(void) as_unmap(as, *addrp, len);
	}       

	/*
	 * Allocate a block address list for the file being mapped.
	 * We don't bother checking whether the allocation succeeded
	 * since the only effect, if it failed for some reason (e.g.,
	 * lack of memory), will be on performance.
	 */
	ip = VTOI(vp);
	/* lock the inode to protect the blocklist during construction */
	ILOCK(ip);
	(void) s5allocmap(ip);
	IUNLOCK(ip);

	vn_a.vp = vp;
	vn_a.offset = off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = (u_char)prot;
	vn_a.maxprot = (u_char)maxprot;
	vn_a.cred = cr;
	vn_a.amp = NULL;

	return as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
}

/* ARGSUSED */
STATIC int
s5addmap(vp, off, as, addrp, len, prot, maxprot, flags, cr)
	struct vnode *vp;
	u_int off;
	struct as *as;
	caddr_t *addrp;
	u_int len;
	u_int prot, maxprot;
	u_int flags;
	struct cred *cr;
{
	if (vp->v_flag & VNOMAP)
		return ENOSYS;
	VTOI(vp)->i_mapcnt += btopr(len);
	return 0;
}

/* ARGSUSED */
STATIC int
s5delmap(vp, off, as, addrp, len, prot, maxprot, flags, cr)
	struct vnode *vp;
	u_int off;
	struct as *as;
	caddr_t *addrp;
	u_int len;
	u_int prot, maxprot;
	u_int flags;
	struct cred *cr;
{
	struct inode *ip;

	if (vp->v_flag & VNOMAP)
		return ENOSYS;

	ip = VTOI(vp);
	ip->i_mapcnt -= btopr(len);
	if (ip->i_mapcnt < 0)
		cmn_err(CE_PANIC, "s5unmap: fewer than 0 mappings");
	return 0;
}
