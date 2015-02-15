/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * Vax 32V Unix filesystem (same as pre-FFS Berkeley)
 */
#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include "tapefs.h"

/*
 * v32 disk inode
 */
#define	VNADDR	13
#define	VFMT	0160000
#define	VIFREG	0100000
#define	VIFDIR	0040000
#define	VIFCHR	0120000
#define	VIFBLK	0160000
#define	VMODE	0777
#define	VSUPERB	1
#define	VROOT		2	/* root inode */
#define	VNAMELEN	14
#define	MAXBLSIZE	1024
int	BLSIZE;
#define	LINOPB	(BLSIZE/sizeof(struct v32dinode))
#define	LNINDIR	(BLSIZE/4)
#define	MAXLNINDIR	(MAXBLSIZE/4)

struct v32dinode {
	unsigned char flags[2];
	unsigned char nlinks[2];
	unsigned char uid[2];
	unsigned char gid[2];
	unsigned char size[4];
	unsigned char addr[40];
	unsigned char atime[4];
	unsigned char mtime[4];
	unsigned char ctime[4];
};

struct	v32dir {
	uint8_t	ino[2];
	char	name[VNAMELEN];
};

int	tapefile;
Fileinf	iget(int ino);
int32_t	bmap(Ram *r, int32_t bno);
void	getblk(Ram *r, int32_t bno, char *buf);

void
populate(char *name)
{
	Fileinf f;

	BLSIZE = 512;	/* 32v */
	if(blocksize){
		/* 1024 for 4.1BSD */
		if(blocksize != 512 && blocksize != 1024)
			error("bad block size");
		BLSIZE = blocksize;
	}
	replete = 0;
	tapefile = open(name, OREAD);
	if (tapefile<0)
		error("Can't open argument file");
	f = iget(VROOT);
	ram->perm = f.mode;
	ram->mtime = f.mdate;
	ram->addr = f.addr;
	ram->data = f.data;
	ram->ndata = f.size;
}

void
popdir(Ram *r)
{
	int i, ino;
	char *cp;
	struct v32dir *dp;
	Fileinf f;
	char name[VNAMELEN+1];

	cp = 0;
	for (i=0; i<r->ndata; i+=sizeof(struct v32dir)) {
		if (i%BLSIZE==0)
			cp = doread(r, i, BLSIZE);
		dp = (struct v32dir *)(cp+i%BLSIZE);
		ino = g2byte(dp->ino);
		if (strcmp(dp->name, ".")==0 || strcmp(dp->name, "..")==0)
			continue;
		if (ino==0)
			continue;
		f = iget(ino);
		strncpy(name, dp->name, VNAMELEN);
		name[VNAMELEN] = '\0';
		f.name = name;
		popfile(r, f);
	}
	r->replete = 1;
}

void
dotrunc(Ram *r)
{
	USED(r);
}

void
docreate(Ram *r)
{
	USED(r);
}

char *
doread(Ram *r, int64_t off, int32_t cnt)
{
	static char buf[Maxbuf+MAXBLSIZE];
	int bno, i;

	bno = off/BLSIZE;
	off -= bno*BLSIZE;
	if (cnt>Maxbuf)
		error("count too large");
	if (off)
		cnt += off;
	i = 0;
	while (cnt>0) {
		getblk(r, bno, &buf[i*BLSIZE]);
		cnt -= BLSIZE;
		bno++;
		i++;
	}
	return buf;
}

void
dowrite(Ram *r, char *buf, int32_t off, int32_t cnt)
{
	USED(r); USED(buf); USED(off); USED(cnt);
}

int
dopermw(Ram *r)
{
	USED(r);
	return 0;
}

/*
 * fetch an i-node
 * -- no sanity check for now
 * -- magic inode-to-disk-block stuff here
 */

Fileinf
iget(int ino)
{
	char buf[MAXBLSIZE];
	struct v32dinode *dp;
	int32_t flags, i;
	Fileinf f;

	seek(tapefile, BLSIZE*((ino-1)/LINOPB + VSUPERB + 1), 0);
	if (read(tapefile, buf, BLSIZE) != BLSIZE)
		error("Can't read inode");
	dp = ((struct v32dinode *)buf) + ((ino-1)%LINOPB);
	flags = g2byte(dp->flags);
	f.size = g4byte(dp->size);
	if ((flags&VFMT)==VIFCHR || (flags&VFMT)==VIFBLK)
		f.size = 0;
	f.data = emalloc(VNADDR*sizeof(int32_t));
	for (i = 0; i < VNADDR; i++)
		((int32_t*)f.data)[i] = g3byte(dp->addr+3*i);
	f.mode = flags & VMODE;
	if ((flags&VFMT)==VIFDIR)
		f.mode |= DMDIR;
	f.uid = g2byte(dp->uid);
	f.gid = g2byte(dp->gid);	
	f.mdate = g4byte(dp->mtime);
	return f;
}

void
getblk(Ram *r, int32_t bno, char *buf)
{
	int32_t dbno;

	if ((dbno = bmap(r, bno)) == 0) {
		memset(buf, 0, BLSIZE);
		return;
	}
	seek(tapefile, dbno*BLSIZE, 0);
	if (read(tapefile, buf, BLSIZE) != BLSIZE)
		error("bad read");
}

/*
 * logical to physical block
 * only singly-indirect files for now
 */

int32_t
bmap(Ram *r, int32_t bno)
{
	unsigned char indbuf[MAXLNINDIR][4];

	if (bno < VNADDR-3)
		return ((int32_t*)r->data)[bno];
	if (bno < VNADDR*LNINDIR) {
		seek(tapefile,
		     ((int32_t *)r->data)[(bno-(VNADDR-3))/LNINDIR]*BLSIZE,
		     0);
		if (read(tapefile, (char *)indbuf, BLSIZE) != BLSIZE)
			return 0;
		return ((indbuf[bno%LNINDIR][1]<<8) + indbuf[bno%LNINDIR][0]);
	}
	return 0;
}
