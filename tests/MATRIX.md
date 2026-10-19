# The test matrix

Each phase's problem space is plotted before its tests (CLAUDE.md,
Testing): its dimensions named and crossed into cells, and every cell
given a disposition -- the test that covers it, an existing test it
maps to, or a deferral with its reason and the work that unblocks it.
A cell with no row here is a hole.

A family is a letter and a phase; a cell is the family's letter and a
number, which never changes once given. A retired cell keeps its row,
marked retired with the reason. Each test's comment names the cells
it covers.

## Family M: the allocators (m1-tree)

Dimensions: the allocator (calloc, realloc, strdup) by what the
system does (gives the memory, refuses it). A refusal is logged to
syslog and ends the process with 1 (plan 10).

| cell | allocator | system | expected | disposition |
|------|-----------|--------|----------|-------------|
| M1 | sh_calloc | gives | zeroed memory | tests/check_mem.c |
| M2 | sh_calloc | refuses | "out of memory" logged, exit 1 | tests/check_mem.c (n times size overflows) |
| M3 | sh_realloc | gives | grown, contents kept | tests/check_mem.c |
| M4 | sh_realloc | refuses | "out of memory" logged, exit 1 | tests/check_mem.c (SIZE_MAX) |
| M5 | sh_strdup | gives | the copy | tests/check_mem.c |
| M6 | sh_strdup | refuses | as M2 | deferred: no string can be made long enough to fail it; the path is M2's, through the same function |
