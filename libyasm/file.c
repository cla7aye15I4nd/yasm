/*
 * File helper functions.
 *
 *  Copyright (C) 2001-2006  Peter Johnson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#undef HAVE_CONFIG_H
#endif

/* Need either unistd.h or direct.h (on Windows) to prototype getcwd() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#elif defined(WIN32) || defined(_WIN32)
#include <direct.h>
#endif

#include <ctype.h>

#define YASM_LIB_INTERNAL
#include "util.h"
/*@unused@*/ RCSID("$Id$");

#include "file.h"


size_t
yasm__splitpath_unix(const char *path, /*@out@*/ const char **tail)
{
    const char *s;
    s = strrchr(path, '/');
    if (!s) {
	/* No head */
	*tail = path;
	return 0;
    }
    *tail = s+1;
    /* Strip trailing ./ on path */
    while ((s-1)>=path && *(s-1) == '.' && *s == '/'
	   && !((s-2)>=path && *(s-2) == '.'))
	s -= 2;
    /* Strip trailing slashes on path (except leading) */
    while (s>path && *s == '/')
	s--;
    /* Return length of head */
    return s-path+1;
}

size_t
yasm__splitpath_win(const char *path, /*@out@*/ const char **tail)
{
    const char *basepath = path;
    const char *s;

    /* split off drive letter first, if any */
    if (isalpha(path[0]) && path[1] == ':')
	basepath += 2;

    s = basepath;
    while (*s != '\0')
	s++;
    while (s >= basepath && *s != '\\' && *s != '/')
	s--;
    if (s < basepath) {
	*tail = basepath;
	if (path == basepath)
	    return 0;	/* No head */
	else
	    return 2;	/* Drive letter is head */
    }
    *tail = s+1;
    /* Strip trailing .\ or ./ on path */
    while ((s-1)>=basepath && *(s-1) == '.' && (*s == '/' || *s == '\\')
	   && !((s-2)>=basepath && *(s-2) == '.'))
	s -= 2;
    /* Strip trailing slashes on path (except leading) */
    while (s>basepath && (*s == '/' || *s == '\\'))
	s--;
    /* Return length of head */
    return s-path+1;
}

/* FIXME: dumb way for now */
char *
yasm__abspath_unix(const char *path)
{
    char *curdir, *abspath;
    static const char pathsep[2] = "/";

    curdir = getcwd(NULL, 0);

    abspath = yasm_xmalloc(strlen(curdir) + strlen(path) + 2);
    strcpy(abspath, curdir);
    strcat(abspath, pathsep);
    strcat(abspath, path);

    free(curdir);

    return abspath;
}

/* FIXME: dumb way for now */
char *
yasm__abspath_win(const char *path)
{
    char *curdir, *abspath, *ch;
    static const char pathsep[2] = "\\";

    curdir = getcwd(NULL, 0);

    abspath = yasm_xmalloc(strlen(curdir) + strlen(path) + 2);
    strcpy(abspath, curdir);
    strcat(abspath, pathsep);
    strcat(abspath, path);

    free(curdir);

    /* Replace all / with \ */
    ch = abspath;
    while (*ch) {
	if (*ch == '/')
	    *ch = '\\';
	ch++;
    }

    return abspath;
}

char *
yasm__combpath_unix(const char *from, const char *to)
{
    const char *tail;
    size_t pathlen, i, j;
    char *out;

    if (to[0] == '/') {
	/* absolute "to" */
	out = yasm_xmalloc(strlen(to)+1);
	/* Combine any double slashes when copying */
	for (j=0; *to; to++) {
	    if (*to == '/' && *(to+1) == '/')
		continue;
	    out[j++] = *to;
	}
	out[j++] = '\0';
	return out;
    }

    /* Get path component; note this strips trailing slash */
    pathlen = yasm__splitpath_unix(from, &tail);

    out = yasm_xmalloc(pathlen+strlen(to)+2);	/* worst case maximum len */

    /* Combine any double slashes when copying */
    for (i=0, j=0; i<pathlen; i++) {
	if (i<pathlen-1 && from[i] == '/' && from[i+1] == '/')
	    continue;
	out[j++] = from[i];
    }
    pathlen = j;

    /* Add trailing slash back in */
    if (pathlen > 0 && out[pathlen-1] != '/')
	out[pathlen++] = '/';

    /* Now scan from left to right through "to", stripping off "." and "..";
     * if we see "..", back up one directory in out unless last directory in
     * out is also "..".
     *
     * Note this does NOT back through ..'s in the "from" path; this is just
     * as well as that could skip symlinks (e.g. "foo/bar/.." might not be
     * the same as "foo").
     */
    for (;;) {
	if (to[0] == '.' && to[1] == '/') {
	    to += 2;	    /* current directory */
	    while (*to == '/')
		to++;		/* strip off any additional slashes */
	} else if (pathlen == 0)
	    break;	    /* no more "from" path left, we're done */
	else if (to[0] == '.' && to[1] == '.' && to[2] == '/') {
	    if (pathlen >= 3 && out[pathlen-1] == '/' && out[pathlen-2] == '.'
		&& out[pathlen-3] == '.') {
		/* can't ".." against a "..", so we're done. */
		break;
	    }

	    to += 3;	/* throw away "../" */
	    while (*to == '/')
		to++;		/* strip off any additional slashes */

	    /* and back out last directory in "out" if not already at root */
	    if (pathlen > 1) {
		pathlen--;	/* strip off trailing '/' */
		while (pathlen > 0 && out[pathlen-1] != '/')
		    pathlen--;
	    }
	} else
	    break;
    }

    /* Copy "to" to tail of output, and we're done */
    /* Combine any double slashes when copying */
    for (j=pathlen; *to; to++) {
	if (*to == '/' && *(to+1) == '/')
	    continue;
	out[j++] = *to;
    }
    out[j++] = '\0';

    return out;
}

char *
yasm__combpath_win(const char *from, const char *to)
{
    const char *tail;
    size_t pathlen, i, j;
    char *out;

    if ((isalpha(to[0]) && to[1] == ':') || (to[0] == '/' || to[0] == '\\')) {
	/* absolute or drive letter "to" */
	out = yasm_xmalloc(strlen(to)+1);
	/* Combine any double slashes when copying */
	for (j=0; *to; to++) {
	    if ((*to == '/' || *to == '\\')
		&& (*(to+1) == '/' || *(to+1) == '\\'))
		continue;
	    if (*to == '/')
		out[j++] = '\\';
	    else
		out[j++] = *to;
	}
	out[j++] = '\0';
	return out;
    }

    /* Get path component; note this strips trailing slash */
    pathlen = yasm__splitpath_win(from, &tail);

    out = yasm_xmalloc(pathlen+strlen(to)+2);	/* worst case maximum len */

    /* Combine any double slashes when copying */
    for (i=0, j=0; i<pathlen; i++) {
	if (i<pathlen-1 && (from[i] == '/' || from[i] == '\\')
	    && (from[i+1] == '/' || from[i+1] == '\\'))
	    continue;
	if (from[i] == '/')
	    out[j++] = '\\';
	else
	    out[j++] = from[i];
    }
    pathlen = j;

    /* Add trailing slash back in, unless it's only a raw drive letter */
    if (pathlen > 0 && out[pathlen-1] != '\\'
	&& !(pathlen == 2 && isalpha(out[0]) && out[1] == ':'))
	out[pathlen++] = '\\';

    /* Now scan from left to right through "to", stripping off "." and "..";
     * if we see "..", back up one directory in out unless last directory in
     * out is also "..".
     *
     * Note this does NOT back through ..'s in the "from" path; this is just
     * as well as that could skip symlinks (e.g. "foo/bar/.." might not be
     * the same as "foo").
     */
    for (;;) {
	if (to[0] == '.' && (to[1] == '/' || to[1] == '\\')) {
	    to += 2;	    /* current directory */
	    while (*to == '/' || *to == '\\')
		to++;		/* strip off any additional slashes */
	} else if (pathlen == 0
		 || (pathlen == 2 && isalpha(out[0]) && out[1] == ':'))
	    break;	    /* no more "from" path left, we're done */
	else if (to[0] == '.' && to[1] == '.'
		 && (to[2] == '/' || to[2] == '\\')) {
	    if (pathlen >= 3 && out[pathlen-1] == '\\'
		&& out[pathlen-2] == '.' && out[pathlen-3] == '.') {
		/* can't ".." against a "..", so we're done. */
		break;
	    }

	    to += 3;	/* throw away "../" (or "..\") */
	    while (*to == '/' || *to == '\\')
		to++;		/* strip off any additional slashes */

	    /* and back out last directory in "out" if not already at root */
	    if (pathlen > 1) {
		pathlen--;	/* strip off trailing '/' */
		while (pathlen > 0 && out[pathlen-1] != '\\')
		    pathlen--;
	    }
	} else
	    break;
    }

    /* Copy "to" to tail of output, and we're done */
    /* Combine any double slashes when copying */
    for (j=pathlen; *to; to++) {
	if ((*to == '/' || *to == '\\') && (*(to+1) == '/' || *(to+1) == '\\'))
	    continue;
	if (*to == '/')
	    out[j++] = '\\';
	else
	    out[j++] = *to;
    }
    out[j++] = '\0';

    return out;
}

FILE *
yasm__fopen_include(const char *iname, const char *from, const char **paths,
		    const char *mode, char **oname)
{
    FILE *f;
    char *combine;
    const char *path;

    /* Try directly relative to from first, then each of the include paths */
    path = from;
    while (path) {
	combine = yasm__combpath(path, iname);
	f = fopen(combine, mode);
	if (f) {
	    *oname = combine;
	    return f;
	}
	yasm_xfree(combine);
	if (!paths)
	    break;
	path = *paths++;
    }

    return NULL;
}

size_t
yasm_fwrite_16_l(unsigned short val, FILE *f)
{
    if (fputc(val & 0xFF, f) == EOF)
	return 0;
    if (fputc((val >> 8) & 0xFF, f) == EOF)
	return 0;
    return 1;
}

size_t
yasm_fwrite_32_l(unsigned long val, FILE *f)
{
    if (fputc((int)(val & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)((val >> 8) & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)((val >> 16) & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)((val >> 24) & 0xFF), f) == EOF)
	return 0;
    return 1;
}

size_t
yasm_fwrite_16_b(unsigned short val, FILE *f)
{
    if (fputc((val >> 8) & 0xFF, f) == EOF)
	return 0;
    if (fputc(val & 0xFF, f) == EOF)
	return 0;
    return 1;
}

size_t
yasm_fwrite_32_b(unsigned long val, FILE *f)
{
    if (fputc((int)((val >> 24) & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)((val >> 16) & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)((val >> 8) & 0xFF), f) == EOF)
	return 0;
    if (fputc((int)(val & 0xFF), f) == EOF)
	return 0;
    return 1;
}
