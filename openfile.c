/*
 *   Copyright (c) 1998 David Parsons. All rights reserved.
 *   
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  3. All advertising materials mentioning features or use of this
 *     software must display the following acknowledgement:
 *     
 *   This product includes software developed by David Parsons
 *   (orc@pell.chi.il.us)
 *
 *  4. My name may not be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *     
 *  THIS SOFTWARE IS PROVIDED BY DAVID PARSONS ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DAVID
 *  PARSONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 */
static const char rcsid[] = "$Id$";
/*
 * openfile() opens a new file, trying to avoid overwriting existing files.
 */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

FILE*
openfile(char* filename, char *actualname)
{
    int end_of_name;
    char* tempname;
    unsigned int seq = 0;
    int fd;
    int idx;
    char *p;

    if (filename) {
	for (p = filename; *p; ++p)
	    if (*p == '/' || *p == '\\')
		break;

	if (*p) {
	    tempname = alloca(strlen(filename)+1);
	    for (p=filename,idx=0; *p; ++idx, ++p)
		tempname[idx] = (*p == '/' || *p == '\\') ? '_' : *p;
	    tempname[idx] = 0;
	    return openfile(tempname, actualname);
	}

	if ((fd = open(filename, O_WRONLY|O_EXCL|O_CREAT, 0644)) >= 0) {
	    if (actualname) strcpy(actualname, filename);
	    return fdopen(fd, "w");
	}
    }
    else
	filename = "part";

    end_of_name = strlen(filename);
    tempname = alloca(end_of_name+10);

    memset(tempname, 0, end_of_name+10);
    strcpy(tempname, filename);

    while (seq < (1<<15)) {
	sprintf(tempname+end_of_name, "%d", ++seq);

	if ((fd = open(tempname, O_WRONLY|O_EXCL|O_CREAT, 0644)) >= 0) {
	    if (actualname) strcpy(actualname, tempname);
	    return fdopen(fd, "w");
	}
    }
    return 0;
} /* openfile */
