# Sprint 1: open questions

Eighteen were asked on 2026-10-18 and seventeen are ruled
(implementation-plan.md section 18). One is left, asked again in
plainer words.

## May a house wait before rolling onto a foundation version that has arrived?

As the plan stands, the moment a new version of a foundation reaches
zrun, every house on that foundation rolls onto it. For a container
that means a short stop and start. For a system it means two reboots,
into outer space and then for real, because a system's roll is done
with the machine stopped.

An example. Foundation `generic` is rebuilt by its schedule every
night at 03:00, and its new version reaches zrun at about 03:40. At
03:40 every container on `generic` stops and starts again, and KIDON,
a system on `generic`, reboots twice, with nobody having asked for it
that night.

Your first message said updates "can be scheduled or manual"; your
answers said houses roll automatically. This is the one place the two
could pull apart, so the question is which you want:

- the foundation's schedule is the only one. A house rolls as soon as
  its foundation's version arrives. A system that must not reboot by
  surprise sits on a foundation that is rebuilt by hand, or one
  scheduled at an hour when a reboot is fine;
- or each house may also say when it rolls, at once, in a window of
  its own ("Sundays at 04:00"), or only when someone runs `shtetl
  roll`, and an arrived foundation version waits for it.

Recommendation: the first. It keeps two triggers, the foundation's
and a person's; a house's own window would be a third, and the queue
of arrived versions would have to wait on a clock as well as on the
house being idle.
