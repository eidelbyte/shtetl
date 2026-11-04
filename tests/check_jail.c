/*
 * Family N of tests/MATRIX.md, N1 - N6: a container's jail's block and
 * its steps (src/core/jail.h). jail(8) is tests/bin's, which logs its
 * words. Where the block's jailconf comes from is check_house's.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include "check.h"
#include "core/jail.h"
#include "core/job.h"
#include "core/parse.h"
#include "core/tree.h"

static struct cap c;
static char file[1024];

/* What every block holds before the jailconf. */
static const char head[] =
	"# written by shtetl for the container web; the recipe's jailconf\n"
	"# is where to change it\n"
	"web {\n"
	"\tpath = \"/containers/web\";\n"
	"\thost.hostname = \"web\";\n"
	"\texec.start = \"/bin/sh /etc/rc\";\n"
	"\texec.stop = \"/bin/sh /etc/rc.shutdown jail\";\n"
	"\texec.clean;\n"
	"\tmount.devfs;\n"
	"\tdevfs_ruleset = 4;\n"
	"\tip4 = inherit;\n"
	"\tip6 = inherit;\n";

/* The file PATH whole, until the next call; NULL if none. */
static const char *
slurp(const char *path)
{
	static char *text;
	size_t n = 0;
	FILE *in;

	free(text);
	text = NULL;
	if ((in = fopen(path, "r")) == NULL)
		return (NULL);
	if (getdelim(&text, &n, '\0', in) < 0) {
		free(text);
		text = NULL;
	}
	(void) fclose(in);
	return (text);
}

/* Whether the block is HEAD, then TAIL. */
static int
block_is(const char *tail)
{
	const char *b = slurp(file);

	return (b != NULL && strncmp(b, head, sizeof (head) - 1) == 0 &&
	    strcmp(b + sizeof (head) - 1, tail) == 0);
}

/* The block written from the shaken recipe TEXT's jailconf, or none. */
static void
write_from(const char *text)
{
	struct sh_node *n = NULL;

	if (text != NULL)
		CHECK((n = sh_parse_buf(text, strlen(text), "house.conf",
		    &c.c_err)) != NULL);
	CHECK(sh_jail_write(file, "web", "/containers/web",
	    n != NULL ? sh_obj_get(n, "jailconf") : NULL, &c.c_err) == 0);
	sh_node_free(n);
}

static struct sh_job job;
static char *logbuf;
static size_t loglen;
static FILE *logf;
static char why[1024];
static int ended;

static void
end(struct sh_job *j, const char *w, void *arg)
{
	(void) j;
	(void) arg;
	ended++;
	(void) snprintf(why, sizeof (why), "%s", w != NULL ? w : "(well)");
}

/* J's steps run to their end, on a kqueue of its own: its why. */
static const char *
run(void)
{
	struct timespec ts = { 20, 0 };
	struct kevent ev;
	int kq;

	CHECK((kq = kqueue()) >= 0);
	ended = 0;
	sh_job_run(&job, kq);
	while (!ended) {
		CHECK(kevent(kq, NULL, 0, &ev, 1, &ts) == 1);
		sh_job_event(ev.udata, kq, &ev);
	}
	(void) close(kq);
	(void) fflush(logf);
	return (why);
}

int
main(void)
{
	char path[1024], cwd[1024], want[2048];

	cap_open(&c);
	(void) snprintf(path, sizeof (path), "%s/tests/bin:%s",
	    getcwd(cwd, sizeof (cwd)), getenv("PATH"));
	CHECK(setenv("PATH", path, 1) == 0);
	CHECK(setenv("FAKE_LOG", scratch_path("fake.log"), 1) == 0);
	(void) snprintf(file, sizeof (file), "%s", scratch_path("web.conf"));

	/* N1, N2: the fixed lines; no jailconf, no line of it */
	write_from(NULL);
	CHECK(block_is("}\n"));
	write_from("pkg { nginx = true; }\n");
	CHECK(block_is("}\n"));

	/* N2: one piece, a tab in, ended */
	write_from("jailconf {\n    \"shtetl.conf\" = \"allow.raw_sockets;\";\n"
	    "}\n");
	CHECK(block_is("\tallow.raw_sockets;\n}\n"));

	/*
	 * N2: the pieces of three layers in their order, one of several
	 * lines with a blank one, one with a newline at its end
	 */
	write_from("jailconf {\n"
	    "    \"shtetl.conf\" = \"allow.raw_sockets;\";\n"
	    "    \"container in shtetl.conf\" = \"allow.sysvipc;\\n\";\n"
	    "    \"containers/web.conf\" = <<EOD\n"
	    "ip4.addr = \"10.0.0.5\";\n"
	    "\n"
	    "allow.mount;\n"
	    "EOD\n"
	    "}\n");
	CHECK(block_is("\tallow.raw_sockets;\n\tallow.sysvipc;\n"
	    "\tip4.addr = \"10.0.0.5\";\n\tallow.mount;\n}\n"));

	/* N3: written again: replaced whole */
	write_from(NULL);
	CHECK(block_is("}\n"));

	/* N4: where it cannot be written */
	cap_reset(&c);
	(void) snprintf(want, sizeof (want), "%s: No such file or directory\n",
	    scratch_path("none/web.conf"));
	CHECK(sh_jail_write(scratch_path("none/web.conf"), "web", "/x", NULL,
	    &c.c_err) == -1 && strcmp(cap_text(&c), want) == 0);

	/* N5: the start's and the stop's children, both named jail */
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	sh_job_init(&job, logf, end, NULL);
	sh_jail_start(&job, file, "web");
	sh_jail_stop(&job, file, "web");
	CHECK(strcmp(run(), "(well)") == 0);
	CHECK(strcmp(logbuf, "# jail\n# jail\n") == 0);
	(void) snprintf(want, sizeof (want), "jail -f %s -cm web\n"
	    "jail -f %s -r web\n", file, file);
	CHECK(strcmp(slurp(scratch_path("fake.log")), want) == 0);

	/* N6: jail(8) refusing: why, and its words in the log */
	CHECK(setenv("FAKE_FAIL", "jail", 1) == 0);
	sh_job_init(&job, logf, end, NULL);
	sh_jail_start(&job, file, "web");
	CHECK(strcmp(run(), "jail: exited 1") == 0);
	CHECK(strstr(logbuf, "# jail\njail: failed\n") != NULL);
	CHECK(unsetenv("FAKE_FAIL") == 0);

	(void) fclose(logf);
	free(logbuf);
	cap_close(&c);
	scratch_clean();
	return (0);
}
