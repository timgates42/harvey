/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <stdio.h>
#include "../common/common.h"
#include "tr2post.h"

char devname[20] = { 'u', 't', 'f', '\0' };
int resolution;
int minx, miny;

struct sjt {
	char *str;
	void (*func)(void *);
};

/* I won't need this if getfields can replace sscanf

extern void picture(Biobufhdr *);
extern void notavail(char *);

void
PSInclude(Biobufhdr *inp) {
	char buf[256];

	Bgetfield(inp, 's', buf, 256);
	if(pageon()) {
		endstring();
		Bprint(Bstdout, "%s\n", buf);
	}
}

struct sjt specialjumptable[] = {
	{"PI", picture},
	{"PictureInclusion", picture},
	{"InlinePicture", NULL},
	{"BeginPath", NULL},
	{"DrawPath", NULL},
	{"BeginObject", NULL},
	{"EndObject", NULL},
	{"NewBaseline", NULL},
	{"DrawText", NULL},
	{"SetText", NULL},
	{"SetColor", NULL},
	{"INFO", NULL},
	{"PS", PSInclude},
	{"Postscript", PSInclude},
	{"ExportPS", notavail("ExportPS")},
	{NULL, NULL}
};
 */

void
devcntl(Biobufhdr *inp) {
	char cmd[50], buf[256], str[MAXTOKENSIZE], *line;
	int c, n;

/*
 *
 * Interpret device control commands, ignoring any we don't recognize. The
 * "x X ..." commands are a device dependent collection generated by troff's
 * \X'...' request.
 *
 */

	Bgetfield(inp, 's', cmd, 50);
	if (debug) Bprint(Bstderr, "devcntl(cmd=%s)\n", cmd);
	switch (cmd[0]) {
	case 'f':		/* mount font in a position */
		Bgetfield(inp, 'd', &n, 0);
		Bgetfield(inp, 's', str, 100);
		mountfont(n, str);
		break;

	case 'i':			/* initialize */
		initialize();
		break;

	case 'p':			/* pause */
		break;

	case 'r':			/* resolution assumed when prepared */
		Bgetfield(inp, 'd', &resolution, 0);
		Bgetfield(inp, 'd', &minx, 0);
		Bgetfield(inp, 'd', &miny, 0);
		break;

	case 's':			/* stop */
	case 't':			/* trailer */
		/* flushtext(); */
		break;

	case 'H':			/* char height */
		Bgetfield(inp, 'd', &n, 0);
		t_charht(n);
		break;

	case 'S':			/* slant */
		Bgetfield(inp, 'd', &n, 0);
		t_slant(n);
		break;

	case 'T':			/* device name */
		Bgetfield(inp, 's', devname, 16);
		if (debug) Bprint(Bstderr, "devname=%s\n", devname);
		break;

	case 'E':			/* input encoding - not in troff yet */
		Bgetfield(inp, 's', str, 100);
/*
		if (strcmp(str, "UTF") == 0)
			reading = UTFENCODING;
		else
			reading = ONEBYTE;
 */
		break;

	case 'X':			/* copy through - from troff */
		if (Bgetfield(inp, 's', str, MAXTOKENSIZE-1) <= 0)
			error(FATAL, "incomplete devcntl line\n");
		if ((line = Brdline(inp, '\n')) == 0)
			error(FATAL, "incomplete devcntl line\n");
		strncpy(buf, line, Blinelen(inp)-1);
		buf[Blinelen(inp)-1] = '\0';
		Bungetc(inp);

		if (strncmp(str, "PI", sizeof("PI")-1) == 0 || strncmp(str, "PictureInclusion", sizeof("PictureInclusion")-1) == 0) {
			picture(inp, str);
		} else if (strncmp(str, "InlinePicture", sizeof("InlinePicture")-1) == 0) {
			error(FATAL, "InlinePicture not implemented yet.\n");
/*			inlinepic(inp, buf);			*/
		} else if (strncmp(str, "BeginPath", sizeof("BeginPath")-1) == 0) {
			beginpath(buf, FALSE);
		} else if (strncmp(str, "DrawPath", sizeof("DrawPath")-1) == 0) {
			drawpath(buf, FALSE);
		} else if (strncmp(str, "BeginObject", sizeof("BeginObject")-1) == 0) {
			beginpath(buf, TRUE);
		} else if (strncmp(str, "EndObject", sizeof("EndObject")-1) == 0) {
			drawpath(buf, TRUE);
		} else if (strncmp(str, "NewBaseline", sizeof("NewBaseline")-1) == 0) {
			error(FATAL, "NewBaseline not implemented yet.\n");
/*			newbaseline(buf);			*/
		} else if (strncmp(str, "DrawText", sizeof("DrawText")-1) == 0) {
			error(FATAL, "DrawText not implemented yet.\n");
/*			drawtext(buf);				*/
		} else if (strncmp(str, "SetText", sizeof("SetText")-1) == 0) {
			error(FATAL, "SetText not implemented yet.\n");
/*			settext(buf);				*/
		} else if (strncmp(str, "SetColor", sizeof("SetColor")-1) == 0) {
			error(FATAL, "SetColor not implemented yet.\n");
/*			newcolor(buf);				*/
/*			setcolor();					*/
		} else if (strncmp(str, "INFO", sizeof("INFO")-1) == 0) {
			error(FATAL, "INFO not implemented yet.\n");
/*			flushtext();				*/
/*			Bprint(outp, "%%INFO%s", buf);	*/
		} else if (strncmp(str, "PS", sizeof("PS")-1) == 0 || strncmp(str, "PostScript", sizeof("PostScript")-1) == 0) {
			if(pageon()) {
				endstring();
				Bprint(Bstdout, "%s\n", buf);
			}
		} else if (strncmp(str, "ExportPS", sizeof("ExportPS")-1) == 0) {	/* dangerous!! */
			error(FATAL, "ExportPS not implemented yet.\n");
/*			if (Bfildes(outp) == 1) {		*/
/*				restore();				*/
/*				Bprint(outp, "%s", buf);	*/
/*				save();				*/
/*			}						*/
		}
/*		 else
			error(WARNING, "Unknown string <%s %s> after x X\n", str, buf);
*/

		break;
	}
	while ((c = Bgetc(inp)) != '\n' && c != Beof);
	inputlineno++;
}
