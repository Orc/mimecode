/*
 *   Copyright (c) 1996 David Parsons. All rights reserved.
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
/*
 * ravel: builds a mime file from a list of input files
 */
#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "mime_encoding.h"
#include <basis/options.h>

#include <sys/utsname.h>
#include <time.h>

#include <paths.h>

extern Encoder base64;
extern Encoder quoted_printable;

struct x_option options[] = {
    { 's', 's', "subject", "SUBJECT", "Set the subject for this message" },
    { 'm', 'm', "message-id", "MESSAGE-ID", "Use this message-id instead of\n"
						 "a randomly generated one." },
    { 'b', 'b', "boundary", "BOUNDARY", "Set the boundary between parts of the mime\n"
					     "message" },
    { 't', 't', "mail-to", "ADDRESS", "Mail the output directly to address\n" },
    { 'f', 'f', "from",    "ADDRESS", "if -t, say the mail is from address\n" },
    { 'p', 'p', "preamble", "TEXT", "Use this text as the preamble to the message\n"
				    "instead of the conventional `this is a MIME\n"
				    "file...' one." },
    { 'o', 'o', "output", "FILE", "Write output to FILE" },
    { 'h', 'h', "help", 0, "print this help message" },
    { 'v', 'v', "verbose", 0, "be somewhat chatty" },
    { 'V', 'V', "version", 0, "Show the current version, then exit" },
} ;
#define NROPTIONS	(sizeof options/sizeof options[0])


void
die(int code)
{
    fprintf(stderr, "\nusage: ravel [options] file [file...]\n\n");
    showopts(stderr, NROPTIONS, options);
    exit (code);
}


static int
readblock(void *ctx, char *block, int size)
{
    context *io = ctx;
    
    return fread(block, 1, size, io->input);
}

static int
writechar(void *ctx, char ch)
{
    context *io = ctx;
    
    return fputc(ch, io->output);
}


main(int argc, char **argv)
{
    int ix;
    char block0[512];
    int j, sz;
    int istextIhope;
    context io;
    int opt;
    struct utsname sys;
    time_t now;
    char *messageid = 0,
	 *boundary = 0,
	 *subject = 0,
	 *preamble = 0,
	 *from = 0;
    char **to = (char**)malloc(1);
    int  nrto = 0;
    int  tomail = 0;
    char *oflag = 0;
    int  verbose = 0;
    char *pgm = basename(argv[0]);

#if 0
    if (strcmp(pgm, "uuencode") == 0) {
	exit(0);
    }
#endif
    x_opterr = 1;
    while ((opt = x_getopt(argc, argv, NROPTIONS, options)) != EOF) {
	switch (opt) {
	case 'f':
	    from = x_optarg;
	    break;
	case 's':
	    subject = x_optarg;
	    break;
	case 'm':
	    messageid = x_optarg;
	    break;
	case 'b':
	    boundary = x_optarg;
	    break;
	case 'p':
	    preamble = x_optarg;
	    break;
	case 'o':
	    if (oflag) {
		fprintf(stderr, "%s: it's meaningless to use two -o's\n", pgm);
		exit(1);
	    }
	    oflag = strdup(x_optarg);
	    break;
	case 't':
	    to = realloc(to, (1+nrto)*sizeof to[0]);
	    to[nrto] = strdup(x_optarg);
	    nrto++;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	case 'V':
	    puts("ravel " VERSION);
	    exit(0);
	default:
	case 'h':
	    die( opt == 'j' ? 0 : 1);
	}
    }
    if (argc == x_optind)
	die(1);

    if (boundary == 0)
	boundary = "Hello, Sailor";
    if (subject == 0)
	subject = "MIME-encoded files";
    if (messageid == 0) {
	static char template[200];

	time(&now);
	uname(&sys);
	sprintf(template, "%u.%u/%u/%s.", getuid(), getgid(), now, sys.nodename);
#if HAS_DOMAINNAME
	if (strlen(sys.domainname) > 0 && strcmp(sys.domainname, "(none)") != 0)
	    strcat(template, sys.domainname);
#endif

	messageid = template;
    }

    if (oflag) {
	if ((io.output = fopen(oflag, "w")) == 0) {
	    perror(oflag);
	    exit(1);
	}
    }
    else if (nrto > 0) {
	if ((io.output = popen(_PATH_SENDMAIL " -U -t", "w")) == 0) {
	    perror("sendmail");
	    exit(1);
	}
	tomail = 1;
    }
    else
	io.output = stdout;

    fprintf(io.output,
	    "Message-ID: <%s>\n"
	   "Subject: %s\n"
           "Mime-Version: 1.0\n"
           "Content-Type: multipart/mixed; boundary=\"%s\"\n",
           messageid, subject, boundary);
    if (from && *from)
	fprintf(io.output, "From: %s\n", from);

    if (to)
	for (ix = 0; ix < nrto; ix++)
	    fprintf(io.output, "To: %s\n", to[ix]);
    writechar(&io,'\n');

    if (preamble)
	fprintf(io.output, "%s\n", preamble);
    else
	fprintf(io.output,
	       "This is a MIME-encoded message.  Decode it with `unravel' or\n"
	       "any other MIME-unpacking software.  Unravel is available at\n"
	       "http://www.pell.chi.il.us/~orc/Code/mimecode\n");

    for (ix = x_optind; ix < argc; ix++) {
	if (verbose)
	    fprintf(stderr, "packaging %s\n", argv[ix]);
	if ((io.input = fopen(argv[ix], "r")) != 0) {
	    fprintf(io.output,
		    "--%s\n"
	           "Content-Disposition: inline; filename=\"%s\"\n",
			boundary, argv[ix]);

	    sz = fread(block0, 1, sizeof block0, io.input);
	    for (istextIhope=1,j=0; j < sz; j++)
		if (!isprint(block0[j]) && block0[j] != '\n'
					&& block0[j] != '\t') {
		    istextIhope=0;
		    break;
		}
	    rewind(io.input);
	    
	    if (istextIhope) {
		if (verbose)
		    fprintf(stderr, "\t\tquoted-printable\n");
		fprintf(io.output,
			"Content-type: text/plain; name=\"%s\"\n"
			"Content-transfer-encoding: quoted-printable\n\n",
			argv[ix]);
		(*quoted_printable.encode)(readblock,writechar,&io);
	    }
	    else {
		if (verbose)
		    fprintf(stderr, "\t\tbase64\n");
		fprintf(io.output,
			"Content-type: application/octet-stream; name=\"%s\"\n"
		        "Content-transfer-encoding: base64\n\n", argv[ix]);
		(*base64.encode)(readblock,writechar,&io);
	    }
	    fclose(io.input);
	}
	else
	    perror(argv[ix]);
    }
    fprintf(io.output, "--%s--\n", boundary);
    fflush(io.output);
    if (tomail) {
	sleep(1);
	pclose(io.output);
    }
    exit(0);
}
