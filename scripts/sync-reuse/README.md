# Synthetic stale Make output-sync handle experiment

Run only in an isolated Windows MSYS2 CI job, with native CMake and mingw32-make:

    cmake -P run.cmake

Not a production workaround. No repository files changed.

The outer Make passes its sync-mutex number through CMake/libuv. Relay first
requires that number to be invalid (otherwise result is inconclusive). It
allocates at most 4096 local unsignalled inheritable Events, keeping them alive,
until the stale number is reused. Allocation failure/reuse failure is not proof.

The relay starts a tiny Make child suspended, assigns it to an exclusive
kill-on-close Windows Job Object, then resumes. All descendants belong to that
job. Failure to assign prevents execution. No persistent system settings change.

* stale: expect a five-second timeout; then signal ONLY the locally-created
  Event and require successful completion within five seconds. This rescue
  strengthens causality beyond merely observing a timeout.
* stripped: same synthetic reuse, remove ONLY the --sync-mutex token before
  launching Make; expect completion within five seconds. All other flags remain.

The relay terminates its private job before exit (also kill-on-close), waits up
to five seconds for the process and closes owned handles. The CMake wrappers
have separate 20/25-second timeouts. Logs live under work/.

The requested events are synthetic and remain local to this test. Even full
success demonstrates a possible mechanism, not proof of the historical hang.
No Windows compile/run has been performed when preparing these files on Linux.
