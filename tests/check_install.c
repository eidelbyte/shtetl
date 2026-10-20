/*
 * The kernel's sync, installworld's backups and the tree removal of
 * src/core/install.c: family K's cells K1 - K8 of tests/MATRIX.md, on
 * trees in the scratch directory. The flag is nodump, which a user may
 * set on the mac and on FreeBSD's ZFS, which has no uchg (box run 4):
 * a flag cleared before the removal, though not one that would stop
 * it. schg, which would, is root's: tests/box/build.sh R5 removes
 * installworld's schg backups.
 */

#include <sys/stat.h>

#include "check.h"
#include "core/install.h"

static struct cap c;

static ino_t
ino(const char *name)
{
	struct stat st;

	return (lstat(scratch_path(name), &st) == 0 ? st.st_ino : 0);
}

static int
there(const char *name)
{
	struct stat st;

	return (lstat(scratch_path(name), &st) == 0);
}

static mode_t
mode(const char *name)
{
	struct stat st;

	return (lstat(scratch_path(name), &st) == 0 ? st.st_mode & 07777 : 0);
}

/* The scratch file NAME holds TEXT. */
static int
holds(const char *name, const char *text)
{
	char buf[256];
	size_t n;
	FILE *f;

	if ((f = fopen(scratch_path(name), "r")) == NULL)
		return (0);
	n = fread(buf, 1, sizeof (buf) - 1, f);
	(void) fclose(f);
	buf[n] = '\0';
	return (strcmp(buf, text) == 0);
}

static int
kernel(void)
{
	char from[1100];

	(void) snprintf(from, sizeof (from), "%s", scratch_path("new"));
	cap_reset(&c);
	return (sh_install_kernel(from, scratch_path("head"), &c.c_err));
}

int
main(void)
{
	ino_t alike, changed, link;

	cap_open(&c);

	/* the head as an earlier roll left it */
	(void) scratch_write("head/boot/kernel/kernel", "kernel 1\n");
	(void) scratch_write("head/boot/kernel/a.ko", "a\n");
	(void) scratch_write("head/boot/kernel/b.ko", "b\n");
	(void) scratch_write("head/boot/kernel/x", "a file\n");
	(void) scratch_write("head/usr/lib/debug/boot/kernel/kernel.debug",
	    "debug\n");
	(void) scratch_write("head/boot/loader", "loader\n");
	(void) scratch_write("head/bin/sh", "world\n");
	CHECK(chmod(scratch_path("head/boot/kernel/a.ko"), 0600) == 0);
	CHECK(symlink("a.ko", scratch_path("head/boot/kernel/same")) == 0);
	CHECK(symlink("a.ko", scratch_path("head/boot/kernel/moved")) == 0);
	/* the kernel installkernel made */
	(void) scratch_write("new/boot/kernel/kernel", "kernel 2\n");
	(void) scratch_write("new/boot/kernel/a.ko", "a\n");
	(void) scratch_write("new/boot/kernel/c.ko", "c\n");
	(void) scratch_write("new/boot/kernel/x/y", "now a directory\n");
	CHECK(chmod(scratch_path("new/boot/kernel/a.ko"), 0444) == 0);
	CHECK(symlink("a.ko", scratch_path("new/boot/kernel/same")) == 0);
	CHECK(symlink("c.ko", scratch_path("new/boot/kernel/moved")) == 0);
	alike = ino("head/boot/kernel/a.ko");
	changed = ino("head/boot/kernel/kernel");
	link = ino("head/boot/kernel/same");

	CHECK(kernel() == 0 && strcmp(cap_text(&c), "") == 0);
	/* K1: a file alike kept, its mode set */
	CHECK(ino("head/boot/kernel/a.ko") == alike &&
	    mode("head/boot/kernel/a.ko") == 0444);
	/* K2: a file changed: a copy in its place */
	CHECK(ino("head/boot/kernel/kernel") != changed &&
	    holds("head/boot/kernel/kernel", "kernel 2\n") &&
	    holds("head/boot/kernel/c.ko", "c\n"));
	/* K3: what the new kernel has not, removed there; kept outside */
	CHECK(!there("head/boot/kernel/b.ko") && there("head/boot/loader") &&
	    holds("head/bin/sh", "world\n"));
	/* K4: a kernel directory the new kernel has none of */
	CHECK(!there("head/usr/lib/debug/boot/kernel") &&
	    there("head/usr/lib/debug/boot"));
	/* K5: a link alike kept; one changed, replaced */
	{
		char to[64];
		ssize_t n;

		CHECK(ino("head/boot/kernel/same") == link);
		n = readlink(scratch_path("head/boot/kernel/moved"), to,
		    sizeof (to) - 1);
		CHECK(n == 4 && strncmp(to, "c.ko", 4) == 0);
	}
	/* K6: a directory where a file was */
	CHECK(holds("head/boot/kernel/x/y", "now a directory\n"));

	/* K7: installworld's backups, and what is none */
	(void) scratch_write("b/boot/kernel.old/kernel", "old\n");
	(void) scratch_write("b/boot/loader.old", "old\n");
	(void) scratch_write("b/boot/loader", "loader\n");
	(void) scratch_write("b/boot/.old", "not one\n");
	(void) scratch_write("b/libexec/ld-elf.so.1.old", "old\n");
	(void) scratch_write("b/libexec/ld-elf32.so.1.old", "old\n");
	(void) scratch_write("b/libexec/ld-elf.so.1", "ld\n");
	(void) scratch_write("b/sbin/init.bak", "old\n");
	(void) scratch_write("b/sbin/init", "init\n");
	CHECK(chflags(scratch_path("b/libexec/ld-elf.so.1.old"),
	    UF_NODUMP) == 0);
	cap_reset(&c);
	CHECK(sh_install_backups(scratch_path("b"), &c.c_err) == 0);
	CHECK(!there("b/boot/kernel.old") && !there("b/boot/loader.old") &&
	    !there("b/libexec/ld-elf.so.1.old") &&
	    !there("b/libexec/ld-elf32.so.1.old") && !there("b/sbin/init.bak"));
	CHECK(there("b/boot/loader") && there("b/boot/.old") &&
	    there("b/libexec/ld-elf.so.1") && there("b/sbin/init"));
	/* no boot, libexec or sbin at all: nothing to do */
	CHECK(sh_install_backups(scratch_path("b/boot"), &c.c_err) == 0);

	/* K8: a tree with flags removed; one not there, done */
	(void) scratch_write("t/d/f", "f\n");
	CHECK(chflags(scratch_path("t/d/f"), UF_NODUMP) == 0 &&
	    chflags(scratch_path("t/d"), UF_NODUMP) == 0);
	CHECK(sh_install_rmtree(scratch_path("t"), &c.c_err) == 0 &&
	    !there("t"));
	CHECK(sh_install_rmtree(scratch_path("t"), &c.c_err) == 0);

	cap_close(&c);
	scratch_clean();
	return (0);
}
