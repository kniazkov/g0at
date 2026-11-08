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
workaround to investigate, not a proven root-cause fix. The Windows full-check driver now uses the same preinstall/direct-CPack
sequence; other hosts retain the package target. Compare the next CI artifacts;
a single green run is insufficient to close #507.

## Optional extended CI checks

Push and pull-request runs retain one observed Release packaging step, the
observer control, relocated-package validation, and the usual test suites.
The extra warm repetitions and clean builds run only when explicitly requested:

1. Open Actions -> Build and test -> Run workflow.
2. Select the branch to check.
3. Enable `extended_windows_packaging` and start the workflow.

The input defaults to false, including for manual runs. Enabling it adds the
checks below on all three Windows profiles to the normal full workflow; it does
not replace the standard suites. Their diagnostic artifacts are uploaded even
if an earlier step failed. Ordinary runs skip both these checks and their
artifact uploaders. The scripts remain available for local Windows diagnosis.

Run 42469227114 (including the rerun after MSYS2 download HTTP 504 failures)
passed all thirty warm packaging pairs and six clean builds with relocated
package validation. On these runners the additional checks took approximately
6-8 minutes per Windows profile; the complete jobs took 51-55 minutes. Moving
these checks behind an explicit input removes that measured additional work,
not the cost of the existing test suites. This evidence supports the workaround
but does not establish the underlying Make wait mechanism or close #507.

## Repeated packaging check

In an extended run, after the initial Release build, all three Windows profiles
run ten additional
preinstall + direct CPack pairs on the same build tree with four jobs. These are
warm packaging runs, not ten clean compilations. The first failure stops the
series and fails CI; no later success can hide it. Each phase has a 120-second
limit, and the CI step has a 15-minute limit. Process observation remains active.

The `package-repetitions-<MSYSTEM>` artifact contains per-phase logs,
`results.tsv` (iteration, phase, UTC start, elapsed seconds, result), and
`events.txt` written before each command so an interrupted phase is identifiable.
The existing relocated-package test checks the final generated package. This
series measures repeatability of the workaround, not the cause of the original
nested Make stall, and does not by itself justify closing #507.

## Clean Windows builds and full-check parity

Windows full-check records `release-preinstall` and `release-package` separately;
a failed preinstall prevents CPack and package validation, while independent
full-check tests still run and the final result remains failure. Linux packaging
is unchanged.

In an extended run, each Windows CI profile also configures two new Release trees, builds preinstall
with four workers, invokes direct CPack, and validates each relocated package.
An existing destination is rejected instead of deleted or reused. The
`package-clean-<MSYSTEM>` artifacts retain phase logs, results, process snapshots
and package test diagnostics. There is no retry; a failure stops the series.
This adds six clean builds across the three profiles, distinct from the thirty
warm packaging repetitions. It does not run the complete full-check test suite
in each clean tree; the standard CI suites remain in the original build trees.
