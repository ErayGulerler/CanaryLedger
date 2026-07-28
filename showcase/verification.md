# Verification record

The two Windows x64 builds used GCC 14.2.0. CMake 3.31.6 with Ninja 1.11.1 built the packaged executable. Bazel 8.4.2 built a second executable from the same revision of the source using the named MinGW configuration.

| Check | Observed result |
| --- | --- |
| CMake release build and CTest | Passed the policy suite, including 18 named cases. |
| Bazel native test | Passed the same 18 cases. |
| Healthy fixture | Both executables returned PROMOTE and exit 0. |
| Incomplete fixture | Both returned HOLD and exit 10 for missing windows and low candidate volume. |
| Regression fixture | Both returned ROLLBACK and exit 20. Checkout exceeded all four rate limits. |
| Invalid fixture | Both rejected the duplicate candidate row on stderr and returned exit 2. |
| Behavioral parity | Exit status, stdout and stderr matched byte for byte across all four fixtures. |

[The replay manifest](manifest.json) records the binary, input, policy and output hashes. The first binary hash belongs to the bundled CMake CLI and the second to the Bazel CLI used for comparison. [The review](release-review.html) is rendered from these replay results.

The Linux Jenkins pipeline and its agent recipe are included. Execution on a live Jenkins controller remains unverified.
