/*
 * openfile() opens a new file, trying to avoid overwriting existing files.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
