CS 161 Problem Set 2 Answers
============================

Leave your name out of this file. Put collaboration notes and credit in
`pset2collab.md`.

**Brief, clear answers preferred!**


C. Parent processes: Per-process metadata design
------------------------------------------------
Keep a `list_links` object `child_link_` and a `list` of children `children_` in each `struct proc`. This way, we reparent children by iterating through `children_`, which is length `C`. So it takes `O(C)` time to reparent on exit. This assumes that processes that aren't immediate children do not get reparented.

C. Parent processes: Synchronization plan
-----------------------------------------
For now, we use the `ptable_lock` for everything, including accesses to `ptable` and reads to `proc::parent_id_`.

D. Wait and exit status: Synchronization plan
---------------------------------------------
Any reads or writes to `ptable`, `children_`, `exit_status`, `interrupted_`, or `p_state` in a given `proc` requires the `ptable_lock` (for now - it is very coarse grained but it works). We need the lock for `interrupted_` to avoid sleep-exit races.

Other notes
-----------


Grading notes
-------------
