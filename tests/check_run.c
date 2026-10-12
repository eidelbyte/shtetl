/*
 * Running a program: tests/MATRIX.md family R, every cell (R1 - R17).
 * The children are the system's own programs; where a case needs a
 * child that misbehaves, it is a small sh script, which is the test's
 * and never shtetl's way of running anything.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "check.h"
#include "core/run.h"

static struct cap c;
static struct sh_run r;

static void
setup(char *const *argv)
{
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_log = -1;
	cap_reset(&c);
}

static void
done(void)
{
	free(r.ru_out);
	r.ru_out = NULL;
}

/* The whole of a scratch file, as a string (one static buffer). */
static const char *
slurp(const char *path, size_t *lenp)
{
	static char buf[65536];
	ssize_t n;
	int fd;

	fd = open(path, O_RDONLY);
	CHECK(fd != -1);
	n = read(fd, buf, sizeof (buf) - 1);
	CHECK(n >= 0);
	buf[n] = '\0';
	(void) close(fd);
	if (lenp != NULL)
		*lenp = (size_t)n;
	return (buf);
}

static int
logfile(const char *name)
{
	int fd;

	fd = open(scratch_path(name), O_WRONLY | O_CREAT | O_TRUNC |
	    O_CLOEXEC, 0644);
	CHECK(fd != -1);
	return (fd);
}

static double
now(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

/* R1 - R5: finding the program, and how it ended. */
static void
test_programs(void)
{
	char *echo[] = { "echo", "hi", NULL };
	char *becho[] = { "/bin/echo", "hi", NULL };
	char *none[] = { "no-such-program-shtetl", NULL };
	char *yes[] = { "true", NULL };
	char *three[] = { "sh", "-c", "exit 3", NULL };
	char *killed[] = { "sh", "-c", "kill -9 $$", NULL };
	char *noexec[] = { NULL, NULL };
	char path[1100];

	setup(echo);
	r.ru_capmax = 64;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(strcmp(r.ru_out, "hi\n") == 0);
	done();
	setup(becho);
	r.ru_capmax = 64;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(strcmp(r.ru_out, "hi\n") == 0);
	done();
	/* R2 */
	setup(none);
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "no-such-program-shtetl: not found on /sbin:/bin:"));
	CHECK(c.c_err.se_count == 1);
	/* R3 */
	(void) snprintf(path, sizeof (path), "%s",
	    scratch_write("noexec", "#!/bin/sh\necho no\n"));
	CHECK(chmod(path, 0644) == 0);
	noexec[0] = path;
	setup(noexec);
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "cannot run "));
	CHECK(cap_has(&c, "noexec: Permission denied"));
	/* R4 */
	setup(yes);
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(c.c_err.se_count == 0);
	CHECK(WIFEXITED(r.ru_status) && WEXITSTATUS(r.ru_status) == 0);
	setup(three);
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "sh: exit status 3\n"));
	CHECK(WEXITSTATUS(r.ru_status) == 3);
	/* R5 */
	setup(killed);
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "sh: killed by SIGKILL\n"));
}

/* R6 - R9, R17: where the output goes. */
static void
test_output(void)
{
	char *both[] = { "sh", "-c", "echo out; echo err >&2", NULL };
	char *split[] = { "sh", "-c", "printf 'a\\nb'; echo err >&2", NULL };
	char *hello[] = { "echo", "hello", NULL };
	char *big[] = { "sh", "-c", "head -c 8388608 /dev/zero", NULL };
	char *toerr[] = { "echo", "to-stderr", NULL };
	struct stat st;
	const char *t;
	int saved, fd;

	/* R6 */
	setup(both);
	r.ru_log = logfile("r6.log");
	CHECK(sh_run(&r, &c.c_err) == 0);
	(void) close(r.ru_log);
	t = slurp(scratch_path("r6.log"), NULL);
	CHECK(strstr(t, "out\n") != NULL && strstr(t, "err\n") != NULL);
	/* R7 */
	setup(split);
	r.ru_log = logfile("r7.log");
	r.ru_capmax = 64;
	CHECK(sh_run(&r, &c.c_err) == 0);
	(void) close(r.ru_log);
	CHECK(r.ru_outlen == 3 && memcmp(r.ru_out, "a\nb", 3) == 0);
	CHECK(strcmp(slurp(scratch_path("r7.log"), NULL), "err\n") == 0);
	done();
	/* R8 */
	setup(hello);
	r.ru_capmax = 4;
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "echo: output larger than 4 bytes"));
	CHECK(r.ru_out == NULL);
	/* R9: logged, and captured */
	setup(big);
	r.ru_log = logfile("r9.log");
	CHECK(sh_run(&r, &c.c_err) == 0);
	(void) close(r.ru_log);
	CHECK(stat(scratch_path("r9.log"), &st) == 0);
	CHECK(st.st_size == 8388608);
	setup(big);
	r.ru_capmax = 16 * 1024 * 1024;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(r.ru_outlen == 8388608);
	done();
	/* R17: standard error swapped for a file while the echo runs */
	setup(toerr);
	r.ru_echo = 1;
	fd = logfile("r17.err");
	saved = dup(STDERR_FILENO);
	CHECK(saved != -1 && dup2(fd, STDERR_FILENO) != -1);
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(dup2(saved, STDERR_FILENO) != -1);
	(void) close(saved);
	(void) close(fd);
	CHECK(strcmp(slurp(scratch_path("r17.err"), NULL), "to-stderr\n") == 0);
}

/* R10 - R13: what the child is given. */
static void
test_child(void)
{
	char *cat[] = { "cat", NULL };
	char *pwd[] = { "pwd", NULL };
	char *env[] = { "env", NULL };
	char *newenv[] = { "FOO=bar", NULL };
	char script[64], *fdtest[] = { "sh", "-c", script, NULL };
	char real[PATH_MAX], want[PATH_MAX + 2];
	int fd;

	/* R10: would wait on a terminal; /dev/null ends at once */
	setup(cat);
	r.ru_capmax = 64;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(r.ru_outlen == 0 && r.ru_out != NULL && r.ru_out[0] == '\0');
	done();
	/* R11 */
	CHECK(mkdir(scratch_path("wd"), 0755) == 0);
	CHECK(realpath(scratch_path("wd"), real) != NULL);
	(void) snprintf(want, sizeof (want), "%s\n", real);
	setup(pwd);
	r.ru_dir = scratch_path("wd");
	r.ru_capmax = sizeof (want);
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(strcmp(r.ru_out, want) == 0);
	done();
	/* R12 */
	setup(env);
	r.ru_env = newenv;
	r.ru_capmax = 4096;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(strcmp(r.ru_out, "FOO=bar\n") == 0);
	done();
	CHECK(setenv("SHTETL_RUN_TEST", "yes", 1) == 0);
	setup(env);
	r.ru_capmax = 1024 * 1024;
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(strstr(r.ru_out, "SHTETL_RUN_TEST=yes\n") != NULL);
	done();
	/* R13: a descriptor of ours, open across exec, is not the child's */
	fd = open(scratch_path("r13"), O_WRONLY | O_CREAT, 0644);
	CHECK(fd > 2);
	(void) snprintf(script, sizeof (script), "echo x >&%d", fd);
	setup(fdtest);
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(cap_has(&c, "sh: exit status "));
	CHECK(fcntl(fd, F_GETFD) != -1);
	(void) close(fd);
}

/* Whether PID is gone, waiting up to two seconds. */
static int
gone(pid_t pid)
{
	double t0 = now();

	while (now() - t0 < 2.0) {
		if (kill(pid, 0) == -1 && errno == ESRCH)
			return (1);
		(void) usleep(20000);
	}
	return (0);
}

/*
 * R14: SIG sent to this process once the child has started a
 * grandchild and written its pid down. Both are stopped, the run is
 * refused as interrupted, and it returns long before the 30 seconds.
 */
static void
interrupt(int sig)
{
	char script[1200], *argv[] = { "sh", "-c", script, NULL };
	const char *pidf;
	pid_t killer, gpid;
	double t0;

	pidf = scratch_path("r14.pid");
	(void) unlink(pidf);
	(void) snprintf(script, sizeof (script),
	    "sleep 30 & echo $! > '%s.tmp'; mv '%s.tmp' '%s'; sleep 30",
	    pidf, pidf, pidf);
	killer = fork();
	CHECK(killer != -1);
	if (killer == 0) {
		while (access(pidf, F_OK) != 0)
			(void) usleep(10000);
		(void) kill(getppid(), sig);
		_exit(0);
	}
	setup(argv);
	t0 = now();
	CHECK(sh_run(&r, &c.c_err) == -1);
	CHECK(now() - t0 < 10.0);
	CHECK(r.ru_signal == sig);
	CHECK(cap_has(&c, "sh: interrupted by SIG"));
	CHECK(waitpid(killer, NULL, 0) == killer);
	gpid = (pid_t)atol(slurp(pidf, NULL));
	CHECK(gpid > 0);
	CHECK(gone(gpid));
}

/* R14, R15: interruption, and the handlers and mask after. */
static void
test_signals(void)
{
	static const int sigs[] = { SIGINT, SIGTERM, SIGQUIT, SIGHUP,
	    SIGPIPE };
	struct sigaction before[5], after;
	sigset_t mb, ma;
	char *yes[] = { "true", NULL };
	size_t i;

	for (i = 0; i < 5; i++)
		CHECK(sigaction(sigs[i], NULL, &before[i]) == 0);
	CHECK(sigprocmask(SIG_BLOCK, NULL, &mb) == 0);

	interrupt(SIGINT);
	interrupt(SIGTERM);
	setup(yes);
	CHECK(sh_run(&r, &c.c_err) == 0);

	for (i = 0; i < 5; i++) {
		CHECK(sigaction(sigs[i], NULL, &after) == 0);
		CHECK(after.sa_handler == before[i].sa_handler);
	}
	CHECK(sigprocmask(SIG_BLOCK, NULL, &ma) == 0);
	for (i = 0; i < 5; i++)
		CHECK(sigismember(&ma, sigs[i]) == sigismember(&mb, sigs[i]));
}

/* R16: a child gone, a process of its still holding the output. */
static void
test_leftover(void)
{
	char *argv[] = { "sh", "-c", "sleep 5 & echo $!", NULL };
	double t0;
	pid_t left;

	setup(argv);
	r.ru_capmax = 64;
	t0 = now();
	CHECK(sh_run(&r, &c.c_err) == 0);
	CHECK(now() - t0 < 3.0);
	left = (pid_t)atol(r.ru_out);
	CHECK(left > 0);
	(void) kill(left, SIGKILL);
	done();
}

int
main(void)
{
	cap_open(&c);
	test_programs();
	test_output();
	test_child();
	test_signals();
	test_leftover();
	cap_close(&c);
	scratch_clean();
	printf("check_run: %d checks\n", checks);
	return (0);
}
