#include <time.h>   /* for struct timespec */
#include <stdint.h> /* for uint64_t */

/* set in the tv_nsec field of a node's mtime */
enum {
	/* we haven't stat the file yet */
	MTIME_UNKNOWN = -1,
	/* the file does not exist */
	MTIME_MISSING = -2,
};

struct node {
	/* shellpath is the escaped shell path, and is populated as needed by nodeescape */
	struct string *path, *shellpath;

	/* modification time */
	struct timespec mtime;
	/* modification time for build log */
	time_t logmtime;

	/* generating edge and dependent edges.
	 *
	 * only gen and nuse are set in parse.c:parseedge; use is allocated and
	 * populated in build.c:computedirty. */
	struct edge *gen, **use;
	size_t nuse;

	/* command hash used to build this output, read from build log */
	uint64_t hash;

	/* ID for .ninja_deps. -1 if not present in log. */
	int32_t id;

	/* does the node need to be rebuilt */
	_Bool dirty;
};

struct edge {
	struct rule *rule;
	struct pool *pool;
	struct environment *env;

	/* input and output nodes */
	struct node **out, **in;
	size_t nout, nin;

	/* index of first implicit output */
	size_t outimpidx;
	/* index of first implicit and order-only input */
	size_t inimpidx, inorderidx;

	/* command hash */
	uint64_t hash;

	/* how many inputs need to be rebuilt or pruned before this edge is ready */
	size_t nblock;
	/* how many inputs need to be pruned before all outputs can be pruned */
	size_t nprune;

	enum {
		FLAG_WORK      = 1<<0, /* scheduled for build */
		FLAG_HASH      = 1<<1, /* calculated the command hash */
		FLAG_DIRTY_IN  = 1<<3, /* dirty input */
		FLAG_DIRTY_OUT = 1<<4, /* missing or outdated output */
		FLAG_DIRTY     = FLAG_DIRTY_IN | FLAG_DIRTY_OUT,
		FLAG_CYCLE     = 1<<5, /* used for cycle detection */
	} flags;

	/* used to coordinate ready work in build() */
	struct edge *worknext;
	/* used for alledges linked list */
	struct edge *allnext;
};

void graphinit(void);

/* create a new node or return existing node */
struct node *mknode(struct string *);
/* lookup a node by name; returns NULL if it does not exist */
struct node *nodeget(char *);
/* update the mtime field of a node */
void nodestat(struct node *);
/* escape a node's path, populating shellpath */
void nodeescape(struct node *);
/* record the usage of a node by an edge */
void nodeuse(struct node *, struct edge *);

/* create a new edge with the given parent environment */
struct edge *mkedge(struct environment *parent);
/* compute the murmurhash64a of an edge comand and store it in the hash field */
void edgehash(struct edge *);
/* add dependencies from $depfile or .ninja_deps as implicit inputs */
void edgeadddeps(struct edge *e, struct node **deps, size_t ndeps);

/* a single linked list of all edges, valid up until build() */
extern struct edge *alledges;
