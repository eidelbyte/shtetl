# probes

The M0 probes of sprints/sprint-1/implementation-plan.md section 14:
what the plan assumes of ZFS, jails and the boot path, shown on a
real FreeBSD before any code leans on it.

    git pull
    sudo sh probes/run.sh            every probe but the VM ones
    sudo sh probes/run.sh P1 P5      just these
    sh probes/run.sh --list          what there is
    sudo sh probes/run.sh --clean    what a crash left

FreeBSD 15.1, as root through sudo. Each probe gets pools of its own,
s2run and s2lake (zrun's and zlake's stand-ins), on swap-backed memory
disks labelled shtetl-probe, mounted under an altroot
(${TMPDIR:-/tmp}/shtetl-probes). They are destroyed after it, and
--clean destroys only those pools and disks, with any s2p* jail or
other mount under the altroot that would keep them busy. run.sh
refuses to start while either pool exists.

## The log

probes/results/<host>-<UTC time>/ holds summary.txt and one log per
probe, the full transcript of every command. summary.txt opens with
the shtetl commit it ran at and ends with

    == N ok, M failed

Between, a line per check:

    P1 ok P1.3 a cut full send resumes
    P1 failed P1.4 ...: got '...', want '...'
    P2 answer P2.7 devfs after the link moved under a running jail: ...

`ok` and `failed` are checks against what the plan expects; `answer`
records a fact the plan has to be told, neither. A setup step that
fails is `failed setup` and ends that probe.

The run commits its directory and pushes it as the person behind
sudo (su to SUDO_USER: their git identity and credentials). SEND=no
keeps it local.

## What a probe needs

| needs | meaning |
|-------|---------|
| - | ZFS and the base system only |
| zfs_rebase | zfs_rebase with -m (tool 69d8223 or later), on PATH or as ZFS_REBASE |
| vm | a scratch VM with root on ZFS; changes bootfs and reboots; runs only when named, with SCRATCH_VM=yes |
## The VM probes

P3 and P4 change the boot pool's bootfs and reboot, so they want a
scratch FreeBSD 15.1 VM with root on ZFS (the installer's layout), at
least twice the root's size in RAM and swap for P4's memory disk, and
this checkout under /home (outside the boot environment, or a stage's
results would stay behind in it). Each is run again after each reboot
it asks for and finds its stage from ZFS.

probes/vm.sh makes that VM on a FreeBSD host with bhyve (vmm, and the
UEFI firmware: pkg install bhyve-firmware): FreeBSD's own ZFS VM image
of the host's release, 20G, 8G of memory, this checkout cloned into
its /home/shtetl, git-lite for the summaries' commit line. It has no
network; the results come back through its disk.

    git pull
    sudo sh probes/vm.sh make       fetched, checked, the checkout in it
    sudo sh probes/vm.sh run        booted; its console on this terminal

The console is a null modem that cu attaches to. Leave it with ~. (~~.
through ssh, whose own ~. ends the session), and the VM runs on; come
back to it with

    sudo sh probes/vm.sh console

Log in as root (no password) on that console, and run the probes,
SEND=no, since the VM cannot push:

    cd /home/shtetl
    SCRATCH_VM=yes SEND=no sh probes/run.sh P3     then reboot
    ... four runs in all, three reboots, each a plain reboot, since the
    loader reads bootfs and reboot -r keeps the root; each run says
    the next step;
    at the second reboot, watch the loader's menu: P3.6 is whether its
    boot environments list v1 and v2
    SCRATCH_VM=yes SEND=no sh probes/run.sh P4     then reboot -r
    ... outer space reboots for real by itself; run P4 again, reboot,
    and run it a third time
    poweroff

The VM is booted again after each reboot, until it powers off (its
boot loop's words are in /var/tmp/shtetl-vm/boot.log). Then, on the
host:

    sudo sh probes/vm.sh results    copied into probes/results, pushed
    sudo sh probes/vm.sh destroy    when it is no longer wanted

If P4's second run finds nothing recorded from outer space, the rc
script could not reach the disk pool there; what it printed is on
the console. Say with the results what the loader's menu showed.

## Writing one

A probe is P<n>-<name>.sh with a header

    # title: what it shows
    # pools: run lake        the pools it wants (none for a VM probe)
    # needs: -

and a function probe(). Its cells are listed in its header comment,
one line each, and every check names its cell (`check P1.3 WHAT GOT
WANT`): the cells are the probe's matrix. lib.sh has the helpers: x
and xs echo and run a command, m and ms do the same for a setup step
that must not fail, prop reads a property, dir a mounted dataset's
directory.
