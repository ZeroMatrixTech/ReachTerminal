# Gateway runtime security baseline (2026-10-21, revised 2026-10-22)

The account Gateway requires Java 21.0.13 or a newer 21.x security update (October 2026 CPU).
Every numeric version component is compared, so 21.0.12.1 is rejected and 21.0.13.0.2 is accepted.
The launcher no longer silently selects `.local/java-runtime-21`. Select a separately
installed, vendor-verified runtime with `REACH_JAVA_HOME=/absolute/java/home`.
`JAVA_HOME` and PATH are then pinned to the executable actually inspected.
The check refuses ambient Java option variables. Do not disable certificate
verification to restore connectivity.

## eMudhra CA compromise (CVE-2026-91403)

Eight eMudhra intermediate CAs (16 certificates including Sectigo cross-signs) have
recoverable private keys. They validate through four roots: AAA Certificate Services
(Sectigo), emSign ECC Root CA - G3, emSign Root TLS CA - G3 and emSign Root CS CA - G3.
OpenJDK lists 21.0.13 and earlier as affected; its fix (8395142, released as
21.0.13.0.2) keeps the roots in `cacerts` and adds the intermediates to `blocked.certs`.
A version number alone therefore does not prove protection, so the guard inspects the
effective trust material and accepts exactly one of:

- `no-affected-root`: the effective trust store (`jssecacerts` before `cacerts`) contains
  none of the four roots, as with Ubuntu runtimes built from the system store after
  USN-8944-1;
- `blocked.certs`: `lib/security/blocked.certs` uses SHA-256 and contains all 48 entries
  OpenJDK 8395142 adds (16 certificates plus alternate signature encodings).

Removing only emSign ECC Root CA - G3 is insufficient while AAA Certificate Services
remains, because the Sectigo cross-signs still chain to it. The 48 entries were checked
against both the OpenJDK commit and the disclosure site's certificate list.
GA builds of 21.0.13 without 8395142 (for example Temurin 21.0.13+9, published
2026-10-22) ship both roots and are rejected until 21.0.13.0.2 is available.

## Running the check

Run `python3 scripts/runtime_guard.py --java /absolute/java/home/bin/java` before
launching. The output records version, executable, eMudhra mitigation, trust-store path
and hash. Run `python3 tests/runtime_guard_test.py` for the version and trust regressions.
A denied runtime cannot start a new Gateway session. Existing processes are not
stopped or silently upgraded. No account databases, credentials or login sessions
are copied or changed by this check.

The previously selected Ubuntu runtime is 21.0.12.1. Its private trust-store copy no
longer contains any of the four roots (backup and evidence under
`.local/security-backups/20261021-java-trust`), but it predates the October CPU and is
still rejected. At review time the Ubuntu archive still offered 21.0.12.1. Install a
vendor build carrying both the October CPU and 8395142, then run the guard and validate
local HTTPS login plus account/market-data sessions. A successful runtime check is a
baseline check, not proof that the Gateway's own JAR dependencies are vulnerability-free.

References:
- https://openjdk.org/groups/vulnerability/advisories/2026-10-21
- https://github.com/openjdk/jdk21u/commit/85974eed59
- https://emudhra.we-have-a-problem.com/
- https://www.oracle.com/security-alerts/cpuoct2026.html
- https://ubuntu.com/security/notices/USN-8944-1
