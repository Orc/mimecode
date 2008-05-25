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

int overwrite = 0;

FILE*
openfile(char* filename, char *defaultname, char *actualname)
{
    int end_of_name;
    char* tempname;
    unsigned int seq = 0;
    int fd;
    int openmode = overwrite ? O_WRONLY|O_CREAT : O_WRONLY|O_EXCL|O_CREAT;
    char *append;

    if (filename) {
	if ((fd = open(filename, openmode, 0644)) >= 0) {
	    if (actualname) strcpy(actualname, filename);
	    return fdopen(fd, "w");
	}
	append="(copy %d)";
    }
    else {
	filename = defaultname;
	append=".%d";
    }

    end_of_name = strlen(filename);
    tempname = alloca(end_of_name+30);

    memset(tempname, 0, end_of_name+30);
    strcpy(tempname, filename);

    while (seq < (1<<15)) {
	sprintf(tempname+end_of_name, append, ++seq);

	if ((fd = open(tempname, O_WRONLY|O_EXCL|O_CREAT, 0644)) >= 0) {
	    if (actualname) strcpy(actualname, tempname);
	    return fdopen(fd, "w");
	}
    }
    return 0;
} /* openfile */
