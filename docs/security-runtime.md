# Gateway runtime security baseline (2026-10-21)

The account Gateway now requires Java 21.0.13 or a newer 21.x security update.
The launcher no longer silently selects `.local/java-runtime-21`. Select a separately
installed, vendor-verified runtime with `REACH_JAVA_HOME=/absolute/java/home`.
`JAVA_HOME` and PATH are then pinned to the executable actually inspected.
The check also rejects the compromised emSign ECC Root CA - G3 in the effective
default trust store (including `jssecacerts`) and refuses ambient Java option
variables. Do not disable certificate verification to restore connectivity.

Run `python3 scripts/runtime_guard.py --java /absolute/java/home/bin/java` before
launching. The output records version, executable, trust-store path and hash.
Run `python3 tests/runtime_guard_test.py` for the version-gate regressions.
A denied runtime cannot start a new Gateway session. Existing processes are not
stopped or silently upgraded. No account databases, credentials or login sessions
are copied or changed by this check.

The previously selected Ubuntu runtime is 21.0.12.1. At review time the local APT
metadata and Temurin/Azul latest-21 metadata still offered that older release.
Do not label it patched. Install a vendor build carrying the October security
updates when available, then run the guard and validate local HTTPS login plus
account/market-data sessions. A successful runtime check is a baseline check,
not proof that the Gateway's own JAR dependencies are vulnerability-free.

References:
- https://www.oracle.com/java/technologies/javase/21-0-13-relnotes.html
- https://www.oracle.com/security-alerts/cpuoct2026.html
- https://ubuntu.com/security/notices/USN-8944-1
