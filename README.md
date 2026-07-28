# CanaryLedger

**A C++20 release gate for checkout reliability.** A release engineer gives it request counts from the current and candidate versions. It returns a decision for each route, the reason behind it, and a single rollout decision that a CI pipeline can enforce.

In the included regression, **125 of 97,000 candidate requests failed across the site (0.13%)**. Checkout alone had **30 failures in 2,000 requests (1.50%)**, up from **4 in 2,000 (0.20%)** on the baseline. The site-wide figure would pass a 1% error budget. Evaluating checkout separately catches the regression. All examples use synthetic data.

## See it in a minute

1. Open [the release review](showcase/release-review.html) in a browser. It shows all three rollout outcomes alongside the observed counts.
2. On Windows x64, open PowerShell in the extracted project folder. Run the bundled executable to reproduce the checkout decision.

~~~powershell
.\demo\windows-x64\canaryledger.exe policy/release.conf examples/regression.csv
$LASTEXITCODE
~~~

The output includes `"decision":"ROLLBACK"` and exits with code `20`. Swap `regression.csv` for `healthy.csv` to get `PROMOTE`, or `incomplete.csv` to get `HOLD`. The CLI reads local files. The HTML can be opened offline.

## Why two builds?

| Tool | Responsibility | Evidence |
| --- | --- | --- |
| CMake | Builds a reusable static policy library, links the CLI, runs CTest and installs the release package | `CMakeLists.txt`, named Ninja release preset |
| Bazel | Builds the same library, CLI and native suite as explicit targets, with pinned modules | `BUILD.bazel`, `MODULE.bazel.lock`, `.bazelversion` |
| Jenkins | Runs both builds in parallel, compares their behavior, applies the chosen rollout gate and archives evidence | `Jenkinsfile`, Linux agent recipe, replay manifest |

This models a native team supporting CMake consumers while introducing Bazel in CI. Both build graphs use one policy implementation. The replay step detects divergence in stdout, stderr or exit status on the same fixtures. A single failing fixture stops packaging.

## Build and reproduce

On Linux, use a C++20 compiler (GCC 12+), CMake 3.25+, Ninja, Python 3.9+ and Bazel 8.4.2. Bazelisk also works when installed as `bazel`. Bazel needs network access on its first build to fetch the module graph. Run from this directory.

~~~sh
cmake --preset release
cmake --build --preset release
ctest --preset release

bazel build //:canaryledger
bazel test //:gate_test
python3 tools/replay.py build/release/canaryledger --compare bazel-bin/canaryledger
~~~

Open `reports/replay/release-review.html` for the regenerated review. The same directory contains JSON decisions and SHA-256 digests for both executables, the policy, each input and its raw stdout. For a CMake-only replay, omit `--compare` and its argument.

**Windows rebuild.** A Visual Studio x64 developer shell with CMake, Ninja and a C++20-capable MSVC can use the same CMake preset. For MinGW-w64, select `g++` as `CXX` and place its `bin` on PATH. With the MSYS2 `mingw64` layout, set `BAZEL_SH` to the matching `usr\bin\bash.exe` and use `bazel test --config=mingw //:gate_test //:canaryledger`. Use `.exe` paths for the replay, and invoke Python with `py -3` or `python`.

## Decision contract

Input is a strict, unquoted CSV with the header shown below. Each row describes one non-overlapping observation window for a route and deployment cohort. Window IDs must denote the same time interval in both cohorts. A slow request took more than 250 ms. A request can be both slow and failed.

~~~csv
window,route,cohort,requests,errors,slow
w01,checkout,baseline,1000,2,15
w01,checkout,candidate,1000,15,80
~~~

| Result | Exit | Rule |
| --- | ---: | --- |
| PROMOTE | 0 | Every observed route has sufficient matched evidence and meets all thresholds. |
| HOLD | 10 | A route has unmatched windows or fewer than 500 requests in either cohort. |
| ROLLBACK | 20 | A sufficiently sampled route exceeds an absolute limit or a permitted increase. |
| Invalid input | 2 | A malformed file, invalid policy or duplicate window prevents evaluation. The CLI writes the reason to stderr. |

1. Match window IDs for each route. Pool counts across windows, then compute rates. A window with ten requests carries one hundredth of the weight of a window with one thousand.
2. Check volume and window coverage first. For eligible routes, the default limits are 1% errors and 5% slow requests. A rise over baseline of more than 0.5 percentage points in errors or 2 points in slow requests also blocks promotion.
3. Each rate rounds up to the next whole basis point (0.01 percentage points). Compare the rounded rates, including their difference. Equality passes. A zero-volume rate is `null`.
4. Across routes, ROLLBACK takes precedence over HOLD, and HOLD over PROMOTE. The CLI reports every observed route in a stable order. The team can edit the five required values in [the policy file](policy/release.conf).

Duplicate `(window,route,cohort)` keys, negative or impossible counts, more than 100,000 data rows, and more than one billion requests per route/cohort are rejected. Window and route identifiers are 1 to 64 ASCII letters, digits, hyphens or underscores. Cohorts are named `baseline` and `candidate`.

**Operational scope.** The upstream exporter supplies non-overlapping, comparable windows and the same latency definition in both cohorts. A route missing from both exports is invisible to this gate. Thresholds and sample floors are explicit business policy. They do not establish statistical significance. The CLI issues a decision, and the demo pipeline uses it to control artifact publication. Production deployment and rollback belong to the calling release system.

## Run on Jenkins

Use a Pipeline-from-SCM or Multibranch Pipeline job in a Git repository containing these files. Set the script path to `Jenkinsfile`. Install Declarative Pipeline, Git and JUnit plugins on the controller. Give a Linux x86-64 agent the label `canary-linux` and the tools listed above. An agent image recipe is included:

~~~sh
docker build --platform linux/amd64 -f ci/Dockerfile.agent -t canaryledger-agent:local .
~~~

Use that image as an inbound agent with the connection arguments supplied by your Jenkins controller. The Jenkins job checks out the revision into a fresh workspace and uses the agent's account for builds. A first run registers the `ROLLOUT` choice parameter. Select a fixture on subsequent runs.

| ROLLOUT | Expected Jenkins behavior |
| --- | --- |
| `healthy` | All checks pass. Archive a Linux CLI, policy, commit ID, decision and checksums in a fingerprinted tarball. |
| `regression` | Stop packaging with ROLLBACK. Archive the decision and replay reports. |
| `incomplete` | Stop packaging with HOLD. Archive the decision and replay reports. |
| `invalid` | Reject the duplicate row. The console contains the parser error. Replay reports are still archived. |

## CV and interview

**CV bullet:** Built a C++20 canary release gate with CMake and Bazel, tested both builds against the same rollout scenarios, and authored a Jenkins pipeline that gates artifact publication on route-level error and latency budgets.

**Two-minute walkthrough:** Open the release review and explain why a 0.13% aggregate error rate masks a checkout failure. Compare `regression.csv` with `incomplete.csv` to show why evidence quality changes the decision. Finish at `Jenkinsfile` and trace the path from parallel builds through the parity check to the approved package.

For verification details, see [the delivery record](showcase/verification.md). For build references, see [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html), [Bazel modules](https://bazel.build/external/module), and [Jenkins Pipeline syntax](https://www.jenkins.io/doc/book/pipeline/syntax/).
