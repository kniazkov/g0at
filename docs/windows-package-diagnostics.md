# Windows Release package diagnostics (#507)

The CI-only `scripts/observe_release_mingw.sh` runs the existing Release script
with the same output pipeline and exit status. It does not retry the build or
change Make options, parallelism, packaging or the 15-minute Actions timeout.
Local build scripts are unaffected.

Each Windows profile uploads `release-observation-<MSYSTEM>` immediately after
this step, on success as well as failure. Compare these artifacts from the same
commit before changing build options:

- `environment.txt`: revision, tool paths and versions, installed MSYS2 packages,
  and the four allowlisted build environment variables.
- `processes.jsonl`: UTC snapshots every 10 seconds, rooted at the observer's
  invoking shell. Rows include PID/parent PID, creation time, command line,
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

CI checks real process enumeration and the stop marker on each Windows profile.
The shell wrapper is also amenable to a stubbed command test: a successful and a
failing `cmd.exe` must retain their exit statuses and output, and both must stop
the observer. Windows process inspection itself requires Windows; Linux shell
checks cannot validate it.
