#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "arg.h"
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "lex.h"
#include "log.h"
#include "parse.h"
#include "tool.h"
#include "util.h"

char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-C dir] [-f buildfile] [-j maxjobs] [-k maxfail]\n", argv0);
	exit(2);
}

static int
openbuilddir(void)
{
	struct string *builddir;
	struct stat st;
	int fd;

	builddir = envvar(rootenv, "builddir");
	if (!builddir)
		return AT_FDCWD;
	if (stat(builddir->s, &st) < 0) {
		if (errno != ENOENT)
			err(1, "stat %s", builddir->s);
		if (makedirs(builddir) < 0)
			exit(1);
		if (mkdir(builddir->s, 0777) < 0)
			err(1, "mkdir %s", builddir->s);
	}
	fd = open(builddir->s, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		err(1, "open %s", builddir->s);

	return fd;
}

static void
builddefault(void)
{
	struct edge *e;
	struct node *n;
	size_t i;

	if (ndeftarg > 0) {
		for (i = 0; i < ndeftarg; ++i)
			buildadd(deftarg[i]);
	} else {
		/* by default build all nodes which are not used by any edges */
		for (e = alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (n->nuse == 0)
					buildadd(n);
			}
		}
	}
}

int
main(int argc, char *argv[])
{
	/* options */
	char *manifest = "build.ninja";
	int maxjobs = 0, maxfail = 1;
	struct tool *tool = NULL;
	bool verbose = false;

	struct node *n;
	int builddirfd, tries;
	char *end;

	argv0 = strrchr(argv[0], '/');
	argv0 = argv0 ? argv0 + 1 : argv[0];
	ARGBEGIN {
	case 'C':
		if (chdir(EARGF(usage())) < 0)
			err(1, "chdir");
		break;
	case 'f':
		manifest = EARGF(usage());
		break;
	case 'j':
		maxjobs = strtol(EARGF(usage()), NULL, 10);
		if (maxjobs <= 0)
			errx(1, "invalid -j parameter");
		break;
	case 'k':
		maxfail = strtol(EARGF(usage()), &end, 10);
		if (*end)
			errx(1, "invalid -k parameter");
		if (maxfail < 0)
			maxfail = 0;
		break;
	case 't':
		tool = toolget(EARGF(usage()));
		goto argdone;
	case 'v':
		verbose = true;
		break;
	default:
		usage();
	} ARGEND
argdone:

	if (!maxjobs) {
#ifdef _SC_NPROCESSORS_ONLN
		int n = sysconf(_SC_NPROCESSORS_ONLN);
		switch (n) {
		case -1: case 0: case 1:
			maxjobs = 2;
			break;
		case 2:
			maxjobs = 3;
			break;
		default:
			maxjobs = n + 2;
			break;
		}
#else
		maxjobs = 2;
#endif
	}

	tries = 0;
retry:
	/* (re-)initialize global graph, environment, and parse structures */
	graphinit();
	envinit();
	parseinit();

	/* parse the manifest */
	lexfile = mkfile(manifest);
	parse(rootenv);
	fileclose(lexfile);

	if (tool)
		return tool->run(argc, argv);

	/* load the build log */
	builddirfd = openbuilddir();
	loginit(builddirfd);
	depsinit(builddirfd);
	if (builddirfd != AT_FDCWD)
		close(builddirfd);

	/* rebuild the manifest if it's dirty */
	n = nodeget(manifest);
	if (n && n->gen) {
		buildadd(n);
		if (n->dirty) {
			build(maxjobs, maxfail, verbose);
			if (++tries > 100)
				errx(1, "manifest '%s' dirty after 100 tries", manifest);
			goto retry;
		}
	}

	/* finally, build any specified targets or the default targets */
	if (argc) {
		for (; *argv; ++argv) {
			n = nodeget(*argv);
			if (!n)
				errx(1, "unknown target: '%s'", *argv);
			buildadd(n);
		}
	} else {
		builddefault();
	}
	build(maxjobs, maxfail, verbose);
	logclose();

	return 0;
}
