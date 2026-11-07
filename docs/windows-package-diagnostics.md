# Windows Release package diagnostics (#507)

The CI-only `scripts/observe_release_mingw.sh` runs the existing Release script
with the same output pipeline and exit status. It does not retry the build or
change the 15-minute Actions timeout. The Windows Release packaging experiment
is described below and also applies to the local Release script.

Each Windows profile uploads `release-observation-<MSYSTEM>` immediately after
this step, on success as well as failure. Compare these artifacts from the same
commit before changing build options:

- `environment.txt`: revision, tool paths and versions, installed MSYS2 packages,
  and the four allowlisted build environment variables.
- `processes.jsonl`: UTC snapshots every 10 seconds, rooted at the observer's
  invoking shell. Its native WINPID is resolved explicitly from MSYS2 `ps`,
  rather than inferred from the background observer parent (which may be an
  intermediate forked shell). Rows include PID/parent PID, creation time, command line,
  cumulative CPU time (Windows 100 ns units), I/O bytes, handle count and thread
  states/wait reasons. The observer itself and its descendants are excluded.
- `observer-console.txt`, `observer-errors.txt`, `observer-finished.txt`: observer
  startup failures, partial snapshot failures and normal observer completion.
- The original build log and generated CMake/package recipes, when available.

The observer stops on the wrapper's stop marker, disappearance/replacement of
its parent process, or after 14 minutes. It checks the stop marker every second
between snapshots. Diagnostics failures do not turn a failed build green.
Actions may forcibly terminate the process tree at its timeout; the uploader
reads snapshots already written and generated recipes directly from the build
directory. Missing completion markers are evidence of interruption, not proof
of a build deadlock.

Thread wait reasons are not stack traces or names of synchronization objects.
This first diagnostic change can distinguish active compilation, waiting nested
Make/CPack processes, and a surviving output pipeline, but cannot alone prove the
cause of a wait. Next, use the observed process chain to target stack/wait-object
capture and controlled output-sync/preinstall experiments. Do not infer a hang
solely from silence: Make buffers output with `--output-sync=target`.

CI launches a native child from MSYS2 and requires its exact PID in a snapshot,
then checks the stop marker on each Windows profile. Control artifacts are
uploaded even on failure. A snapshot of only the root shell cannot pass.
The shell wrapper is also amenable to a stubbed command test: a successful and a
failing `cmd.exe` must retain their exit statuses and output, and both must stop
the observer. Windows process inspection itself requires Windows; Linux shell
checks cannot validate it.

## Captured stall and next experiment

Run 42402498674 captured 84 UCRT64 and 83 MINGW32 snapshots. Both jobs
reached 100% compilation and timed out in packaging. Their final snapshots
contain the same chain: outer Make package -> CPack -> cmake preinstall ->
Make preinstall -> Make all. CPU and I/O counters in the final two snapshots
are unchanged. This locates the stall inside the nested preinstall build;
it does not identify the mutex/jobserver/handle responsible.

The next experiment changes the Windows Release script to run synchronized
preinstall first and then invoke CPack directly with the generated config and
Release configuration. CPack may still run preinstall internally, but no outer
package Make process holds output synchronization resources around CPack.
Parallelism and failure propagation remain enabled. This is a candidate
workaround to investigate, not a proven root-cause fix. The full-check driver
still exercises the original package target. Compare the next CI artifacts;
a single green run is insufficient to close #507.
