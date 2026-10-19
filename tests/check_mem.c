/*
 * The allocators, src/core/mem.c: family M of tests/MATRIX.md.
 */

#include <sys/wait.h>

#include <stdint.h>
#include <syslog.h>

#include "check.h"
#include "core/mem.h"

/*
 * What a child running FN dies of: its exit status, and in LOG what
 * it logged, caught from its standard error by openlog's LOG_PERROR.
 */
static int
dies(void (*fn)(void), char *log, size_t len)
{
	int fd[2], status;
	size_t got;
	ssize_t n;
	pid_t pid;

	CHECK(pipe(fd) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		(void) dup2(fd[1], 2);
		openlog("check_mem", LOG_PERROR, LOG_USER);
		fn();
		_exit(0);
	}
	(void) close(fd[1]);
	for (got = 0; got < len - 1 &&
	    (n = read(fd[0], log + got, len - 1 - got)) > 0; got += n)
		;
	log[got] = '\0';
	(void) close(fd[0]);
	CHECK(waitpid(pid, &status, 0) == pid);
	return (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}

static void
calloc_overflow(void)
{
	(void) sh_calloc(SIZE_MAX, 2);
}

static void
realloc_huge(void)
{
	(void) sh_realloc(NULL, SIZE_MAX);
}

int
main(void)
{
	char log[4096], *p;
	size_t i;

	/* M1: given, and zeroed. */
	p = sh_calloc(64, 2);
	for (i = 0; i < 128; i++)
		CHECK(p[i] == 0);

	/* M3: grown, what it held kept. */
	(void) memcpy(p, "kept", 5);
	p = sh_realloc(p, 1 << 20);
	CHECK(strcmp(p, "kept") == 0);
	free(p);

	/* M5 */
	p = sh_strdup("shtetl");
	CHECK(strcmp(p, "shtetl") == 0);
	free(p);

	/* M2, M4: refused, logged, and the process ends with 1. */
	CHECK(dies(calloc_overflow, log, sizeof (log)) == 1);
	CHECK(strstr(log, "out of memory") != NULL);
	CHECK(dies(realloc_huge, log, sizeof (log)) == 1);
	CHECK(strstr(log, "out of memory") != NULL);
	return (0);
}
