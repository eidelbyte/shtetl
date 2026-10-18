# spike

The runtime spike of sprints/sprint-1/implementation-plan.md section
14 (S1), for the choice section 10 leaves open: whether shtetl's
actors run on Erlang/OTP, written in Elixir, with C kept behind ports.
Throwaway: the ruling decides what of it stays.

- zport/zport.c: the ZFS port. One OS process speaking Erlang's
  external term format on stdin and stdout ({packet, 4}) through
  erl_interface's ei, calling libzfs_core and libzfs. Binary both ways;
  nothing printed, nothing parsed but terms. Its requests are listed at
  its top.
- roll/: the release. Roll.Zfs owns the port; Roll.Foundation is a
  gen_statem over a toy foundation; a rest_for_one supervisor holds
  the two.

## Running it

On the box, with Erlang and Elixir from packages and /usr/src for the
OpenZFS headers:

    sudo pkg install elixir
    sudo sh probes/run.sh S1

probes/S1-runtime.sh copies spike/ onto its throwaway pool and builds
it there (HOME there too), sets up a toy foundation, starts the
release, and rolls it five times: once left alone, and once with the
actor killed after each of its steps. Its cells are listed at its top.

## What it measures

The release's size on disk, the VM's resident memory idle, the seconds
from its start to its first rpc and what one rpc from the shell costs,
and the port's round trip: the numbers section 10's "tens of
megabytes, to be measured" waits on.

## Findings from writing it

Before any run:

- The actor keeps no sequence of its own. After each step it reads
  ZFS to choose the next (Roll.Foundation.where/0), so a crash and a
  start are one code path; the kill tests check that.
- One roll at a time takes no code. Each step queues the next as an
  internal event, which gen_statem runs before any message waiting, so
  a roll message is only ever seen when the actor is idle.
- The same property is a limit: while a step runs, the actor answers
  nothing. A toy's steps take milliseconds; a foundation's build takes
  an hour. In shtetl a long step has to be a child process the actor
  watches, the actor answering while it waits.
- written@ needs a txg sync before it is read, as main found (its
  sh_zfs_sync); the port has {sync, Pool} and the actor calls it first.

## Results

None yet: the Elixir has not been compiled (no Erlang on the mac),
and zport.c has only been compiled on the mac against the mac's
OpenZFS headers and a stub of ei.h. The box run is the first build of
either.
