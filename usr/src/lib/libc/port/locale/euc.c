/*
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2002-2004 Tim J. Robbins. All rights reserved.
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "lint.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/types.h>
#include "runetype.h"
#include "mblocal.h"

#define	MIN(a, b)	((a) < (b) ? (a) : (b))

static size_t	_EUC_mbrtowc(wchar_t *_RESTRICT_KYWD,
		    const char *_RESTRICT_KYWD,
		    size_t, mbstate_t *_RESTRICT_KYWD);
static int	_EUC_mbsinit(const mbstate_t *);
static size_t	_EUC_wcrtomb(char *_RESTRICT_KYWD, wchar_t,
		    mbstate_t *_RESTRICT_KYWD);

typedef struct {
	int	count[4];
	wchar_t	bits[4];
	wchar_t	mask;
} _EucInfo;

typedef struct {
	wchar_t	ch;
	int	set;
	int	want;
} _EucState;

int
_EUC_init(_RuneLocale *rl)
{
	_EucInfo *ei;
	int x, new__mb_cur_max;
	char *v, *e;

	if (rl->__variable == NULL)
		return (EINVAL);

	v = (char *)rl->__variable;

	while (*v == ' ' || *v == '\t')
		++v;

	if ((ei = malloc(sizeof (_EucInfo))) == NULL)
		return (errno == 0 ? ENOMEM : errno);

	new__mb_cur_max = 0;
	for (x = 0; x < 4; ++x) {
		ei->count[x] = (int)strtol(v, &e, 0);
		if (v == e || !(v = e)) {
			free(ei);
			return (EINVAL);
		}
		if (new__mb_cur_max < ei->count[x])
			new__mb_cur_max = ei->count[x];
		while (*v == ' ' || *v == '\t')
			++v;
		ei->bits[x] = (int)strtol(v, &e, 0);
		if (v == e || !(v = e)) {
			free(ei);
			return (EINVAL);
		}
		while (*v == ' ' || *v == '\t')
			++v;
	}
	ei->mask = (int)strtol(v, &e, 0);
	if (v == e || !(v = e)) {
		free(ei);
		return (EINVAL);
	}
	rl->__variable = ei;
	rl->__variable_len = sizeof (_EucInfo);
	_CurrentRuneLocale = rl;
	__ctype[520] = new__mb_cur_max;
	__mbrtowc = _EUC_mbrtowc;
	__wcrtomb = _EUC_wcrtomb;
	__mbsinit = _EUC_mbsinit;
	charset_is_ascii = 0;
	return (0);
}

static int
_EUC_mbsinit(const mbstate_t *ps)
{

	return (ps == NULL || ((const _EucState *)ps)->want == 0);
}

#define	CEI	((_EucInfo *)(_CurrentRuneLocale->__variable))

#define	_SS2	0x008e
#define	_SS3	0x008f

#define	GR_BITS	0x80808080 /* XXX: to be fixed */

static int
_euc_set(uint_t c)
{

	c &= 0xff;
	return ((c & 0x80) ? c == _SS3 ? 3 : c == _SS2 ? 2 : 1 : 0);
}

static size_t
_EUC_mbrtowc(wchar_t *_RESTRICT_KYWD pwc, const char *_RESTRICT_KYWD s,
    size_t n, mbstate_t *_RESTRICT_KYWD ps)
{
	_EucState *es;
	int i, set, want;
	wchar_t wc;
	const char *os;

	es = (_EucState *)ps;

	if (es->want < 0 || es->want > MB_CUR_MAX || es->set < 0 ||
	    es->set > 3) {
		errno = EINVAL;
		return ((size_t)-1);
	}

	if (s == NULL) {
		s = "";
		n = 1;
		pwc = NULL;
	}

	if (n == 0)
		/* Incomplete multibyte sequence */
		return ((size_t)-2);

	os = s;

	if (es->want == 0) {
		want = CEI->count[set = _euc_set(*s)];
		if (set == 2 || set == 3) {
			--want;
			if (--n == 0) {
				/* Incomplete multibyte sequence */
				es->set = set;
				es->want = want;
				es->ch = 0;
				return ((size_t)-2);
			}
			++s;
			if (*s == '\0') {
				errno = EILSEQ;
				return ((size_t)-1);
			}
		}
		wc = (unsigned char)*s++;
	} else {
		set = es->set;
		want = es->want;
		wc = es->ch;
	}
	for (i = (es->want == 0) ? 1 : 0; i < MIN(want, n); i++) {
		if (*s == '\0') {
			errno = EILSEQ;
			return ((size_t)-1);
		}
		wc = (wc << 8) | (unsigned char)*s++;
	}
	if (i < want) {
		/* Incomplete multibyte sequence */
		es->set = set;
		es->want = want - i;
		es->ch = wc;
		return ((size_t)-2);
	}
	wc = (wc & ~CEI->mask) | CEI->bits[set];
	if (pwc != NULL)
		*pwc = wc;
	es->want = 0;
	return (wc == L'\0' ? 0 : s - os);
}

static size_t
_EUC_wcrtomb(char *_RESTRICT_KYWD s, wchar_t wc, mbstate_t *_RESTRICT_KYWD ps)
{
	_EucState *es;
	wchar_t m, nm;
	int i, len;

	es = (_EucState *)ps;

	if (es->want != 0) {
		errno = EINVAL;
		return ((size_t)-1);
	}

	if (s == NULL)
		/* Reset to initial shift state (no-op) */
		return (1);

	m = wc & CEI->mask;
	nm = wc & ~m;

	if (m == CEI->bits[1]) {
CodeSet1:
		/* Codeset 1: The first byte must have 0x80 in it. */
		i = len = CEI->count[1];
		while (i-- > 0) {
			*(unsigned char *)s = (nm >> (i << 3)) | 0x80;
			s++;
		}
	} else {
		if (m == CEI->bits[0])
			i = len = CEI->count[0];
		else if (m == CEI->bits[2]) {
			i = len = CEI->count[2];
			*(unsigned char *)s = _SS2;
			s++;
			--i;
			/* SS2 designates G2 into GR */
			nm |= GR_BITS;
		} else if (m == CEI->bits[3]) {
			i = len = CEI->count[3];
			*(unsigned char *)s = _SS3;
			s++;
			--i;
			/* SS3 designates G3 into GR */
			nm |= GR_BITS;
		} else
			goto CodeSet1;	/* Bletch */
		while (i-- > 0)
			*s++ = (nm >> (i << 3)) & 0xff;
	}
	return (len);
}