/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define RAM_BLOCK_LEN 32768
#define RAM_MAGIC 0xbedabb1e
#define INVALID_FILE "Invalid ram file"
#define NBLOCK 4096

struct RamFile {
	unsigned int magic;
	char *n;
	struct RamFile* parent;
	struct RamFile* sibling;
	uint64_t length;
	uint64_t alloclength;
	int perm;
	int opencount;
	int deleteonclose;
	// A file is up to 4K 8192-byte blocks. That's 32M. Enough.
	void* blocks[NBLOCK];
	union {
		struct RamFile* firstchild;
	};
};

static struct RamFile* ramroot;
static QLock ramlock;
static int devramdebug = 0;

static void
printramfile(int offset, struct RamFile* file)
{
	int i;
	for(i = 0; i < offset; i++) {
		print(" ");
	}
	print("ramfile: %x, magic:%x, name: %s, parent: %x, sibling:%x, length:%d, alloclength: %d, perm: %o\n",
	      file, file->magic, file->n, file->parent, file->sibling, file->length, file->alloclength, file->perm);
}

static void
debugwalkinternal(int offset, struct RamFile* current)
{
	printramfile(offset, current);
	for(current = current->firstchild; current != nil; current = current->sibling) {
		if(current->perm & DMDIR) {
			debugwalkinternal(offset + 1, current);
		} else {
			printramfile(offset, current);
		}
	}
}

static void
debugwalk()
{
	print("***********************\n");
	debugwalkinternal(0, ramroot);
}

static void
raminit(void)
{
	ramroot = (struct RamFile*)malloc(sizeof(struct RamFile));
	if(ramroot == nil) {
		error(Eperm);
	}
	ramroot->n = smalloc(2);
	kstrdup(&ramroot->n, ".");
	ramroot->magic = RAM_MAGIC;
	ramroot->length = 0;
	ramroot->alloclength = 0;
	ramroot->perm = DMDIR | 0777;
	ramroot->firstchild = nil;
}

static void
ramreset(void)
{
}

static Chan*
ramattach(char* spec)
{
	Chan* c;
	char* buf;

	c = newchan();
	mkqid(&c->qid, (int64_t)ramroot, 0, QTDIR);
	c->dev = devtabget('@', 0);
	if(spec == nil)
		spec = "";
	buf = malloc(1 + UTFmax + strlen(spec) + 1);
	if(buf == nil) {
		error(Eperm);
	}
	sprint(buf, "#@%s", spec);
	c->path = newpath(buf);
	c->mode = 0777;
	free(buf);
	return c;
}

static int
ramgen(Chan* c, char* name, Dirtab* tab, int ntab, int pos, Dir* dp)
{
	Qid qid;
	int i;

	struct RamFile* current = (struct RamFile*)c->qid.path;
	if(pos == DEVDOTDOT) {
		if(current->parent == nil) {
			mkqid(&qid, (uintptr_t)current, 0, QTDIR);
			devdir(c, qid, "#@", 0, eve, current->perm, dp);
			return 1;
		} else {
			mkqid(&qid, (uintptr_t)current->parent, 0, QTDIR);
			devdir(c, qid, current->n, 0, eve, current->perm, dp);
			return 1;
		}
	}
	if(current->perm & DMDIR) {
		current = current->firstchild;
		if(current == nil) {
			return -1;
		}
	}
	for(i = 0; i < pos; i++) {
		current = current->sibling;
		if(current == nil) {
			return -1;
		}
	}
	mkqid(&qid, (uintptr_t)current, 0, current->perm & DMDIR ? QTDIR : 0);
	devdir(c, qid, current->n, current->length, eve, current->perm, dp);
	if(name == nil || strcmp(current->n, name) == 0) {
		return 1;
	} else {
		return 0;
	}
}

static Walkqid*
ramwalk(Chan* c, Chan* nc, char** name, int nname)
{
	Proc* up = externup();
	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}
	Walkqid* wqid = devwalk(c, nc, name, nname, 0, 0, ramgen);
	poperror();
	qunlock(&ramlock);
	return wqid;
}

static int32_t
ramstat(Chan* c, uint8_t* dp, int32_t n)
{
	Proc* up = externup();
	Dir dir;
	Qid qid;
	struct RamFile* current = (struct RamFile*)c->qid.path;

	if(current->magic != RAM_MAGIC)
		error(INVALID_FILE);

	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}

	mkqid(&qid, c->qid.path, 0, current->perm & DMDIR ? QTDIR : 0);
	devdir(c, qid, current->n, current->length, eve, current->perm, &dir);

	int32_t ret = convD2M(&dir, dp, n);
	poperror();
	qunlock(&ramlock);
	return ret;
}

static int32_t
ramwstat(Chan* c, uint8_t* dp, int32_t n)
{
       Proc* up = externup();
       Dir d;
       struct RamFile* current = (struct RamFile*)c->qid.path;
       char *strs;

       if(current->magic != RAM_MAGIC)
               error(INVALID_FILE);

       qlock(&ramlock);
       if(waserror()) {
               qunlock(&ramlock);
               nexterror();
       }

       strs = smalloc(n);
       n = convM2D(dp, n, &d, strs);
       if(n == 0)
               error(Eshortstat);
       if(d.mode != (uint32_t)~0UL)
               current->perm = d.mode;
       if(d.uid && *d.uid)
               error(Eperm);
       if(d.name && *d.name && strcmp(current->n, d.name) != 0) {
               if(strchr(d.name, '/') != nil)
                       error(Ebadchar);
	       // Invalid names should have been caught in convM2D()
	       kstrdup(&current->n, d.name);
       }

       qunlock(&ramlock);
       free(strs);
       poperror();
       return n;
}

static Chan*
ramopen(Chan* c, int omode)
{
	Proc* up = externup();
	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}
	Chan* ret = devopen(c, omode, nil, 0, ramgen);
	struct RamFile* file = (struct RamFile*)c->qid.path;
	if(file->magic != RAM_MAGIC)
		panic("Invalid ram file");
	qunlock(&ramlock);
	poperror();
	return ret;
}

static void delete(struct RamFile* file)
{
	Proc* up = externup();
	struct RamFile* prev = file->parent->firstchild;

	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}
	if(prev == file) {
		// This is the first file - make any sibling the first child
		file->parent->firstchild = file->sibling;
	} else {
		// Find previous file
		for(; prev != nil && prev->sibling != file; prev = prev->sibling)
			;
		if(prev == nil) {
			error(Eperm);
		} else {
			prev->sibling = file->sibling;
		}
	}
	if(!(file->perm & DMDIR)) {
		for(int i = 0; i < nelem(file->blocks); i++)
			free(file->blocks[i]);
	}
	file->magic = 0;
	free(file->n);
	free(file);
	if(devramdebug)
		debugwalk();
	poperror();
	qunlock(&ramlock);
}

static void
ramclose(Chan* c)
{
	struct RamFile* file = (struct RamFile*)c->qid.path;
	if(file->magic != RAM_MAGIC)
		error(INVALID_FILE);
	if(file->deleteonclose) {
		delete(file);
	}
}

static int32_t
ramreadblock(Chan* c, void* buf, int32_t n, int64_t off)
{
	Proc* up = externup();
	// first block, last block
	int fb = off >> 13, fl = (off + n) >> 13, offinblock = off & 0x1fff;

	// adjust read size and offset.
	// we only read one block for now.
	if(fl != fb) {
		n = 8192 - (off & 0x1fff);
	}

	if (fb > NBLOCK)
		return 0;

	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}

	if(c->qid.type == QTDIR) {
		int32_t len = devdirread(c, buf, n, nil, 0, ramgen);
		poperror();
		qunlock(&ramlock);
		return len;
	}
	// Read file
	struct RamFile* file = (void*)c->qid.path;

	if (off + n > file->length)
		n = file->length - off;

	if(file->magic != RAM_MAGIC)
		error(INVALID_FILE);
	if(n > 0 && off < file->length && file->blocks[fb])
		memmove(buf, file->blocks[fb] + offinblock, n);
	else
		n = 0;
	poperror();
	qunlock(&ramlock);
	return n;
}

static int32_t
ramread(Chan* c, void* v, int32_t n, int64_t off)
{
	int32_t total = 0, amt;

	while (total < n) {
		amt = ramreadblock(c, v + total, n - total, off + total);
		if (amt <= 0)
			break;
		total += amt;
	}
	return total;
}

typedef double Align;
typedef union Header Header;

union Header {
	struct {
		Header* next;
		uint size;
	} s;
	Align al;
};

static int32_t
ramwriteblock(Chan* c, void* v, int32_t n, int64_t off)
{
	Proc* up = externup();
	struct RamFile* file = (void*)c->qid.path;
	// first block, last block
	int fb = off >> 13, fl = (off + n) >> 13, offinblock = off & 0x1fff;

	// we only write one block.
	if(fl != fb) {
		n = 8192 - (off & 0x1fff);
	}

	if (fb > NBLOCK)
		return 0;

	qlock(&ramlock);
	if(waserror()) {
		qunlock(&ramlock);
		nexterror();
	}
	if(file->magic != RAM_MAGIC)
		error(INVALID_FILE);
	if(devramdebug)
		print("ramwrite: v %p n %#x off %#llx\n", v, n, off);
	if(!file->blocks[fb]) {
		file->blocks[fb] = mallocz(8192, 1);
		if(!file->blocks[fb])
			error("enomem");
	}
	if(devramdebug) {
		print("length of start of write=%do\n", offinblock);
	}
	memmove(file->blocks[fb] + offinblock, v, n);
	if ((off+n)>file->length)
		file->length=off+n;
	poperror();
	qunlock(&ramlock);
	return n;
}

static int32_t
ramwrite(Chan* c, void* v, int32_t n, int64_t off)
{
	int32_t total = 0, amt;

	if(devramdebug)
		print("Write %#x %#x %#x \n", v + total, n - total, off + total);
	while (total < n) {
		if(devramdebug)
			print("Write block %#x %#x %#x total is %#x\n", v + total, n - total, off + total, total);
		amt = ramwriteblock(c, v + total, n - total, off + total);
		if(devramdebug)
			print("amt %#x\n", amt);
		if (amt <= 0)
			break;
		total += amt;
	}
	if(devramdebug)
		print("ramwrite %#x\n", total);
	return total;
}

void
ramcreate(Chan* c, char* name, int omode, int perm)
{
	Proc* up = externup();

	if(c->qid.type != QTDIR)
		error(Eperm);

	struct RamFile* parent = (struct RamFile*)c->qid.path;
	if(parent->magic != RAM_MAGIC)
		error(INVALID_FILE);

	omode = openmode(omode);
	struct RamFile* file = (struct RamFile*)malloc(sizeof(struct RamFile));
	if(file == nil) {
		error(Eperm);
	}
	file->length = 0;
	file->magic = RAM_MAGIC;
	file->n = smalloc(strlen(name)+1);
	kstrdup(&file->n, name);
	file->perm = perm;
	file->parent = parent;

	qlock(&ramlock);
	if(waserror()) {
		free(file->n);
		free(file);
		qunlock(&ramlock);
		nexterror();
	}

	file->sibling = parent->firstchild;
	parent->firstchild = file;
	mkqid(&c->qid, (uintptr_t)file, 0, file->perm & DMDIR ? QTDIR : 0);

	c->offset = 0;
	c->mode = omode;
	c->flag |= COPEN;
	qunlock(&ramlock);

	poperror();
}

void
ramshutdown(void)
{
}

void
ramremove(Chan* c)
{
	struct RamFile* doomed = (struct RamFile*)c->qid.path;
	if(doomed->magic != RAM_MAGIC)
		error(INVALID_FILE);
	if(doomed->opencount == 0) {
		delete(doomed);
	}
	doomed->deleteonclose = 1;
}

Dev ramdevtab = {
    .dc = '@',
    .name = "ram",

    .reset = ramreset,
    .init = raminit,
    .shutdown = ramshutdown,
    .attach = ramattach,
    .walk = ramwalk,
    .stat = ramstat,
    .open = ramopen,
    .create = ramcreate,
    .close = ramclose,
    .read = ramread,
    .bread = devbread,
    .write = ramwrite,
    .bwrite = devbwrite,
    .remove = ramremove,
    .wstat = ramwstat,
};
