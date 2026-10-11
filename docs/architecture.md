# Architecture

## Runtime

The browser loads static HTML/CSS/ES modules from the C++ cpp-httplib server.
No Node/npm runtime, CDN, telemetry or external fonts are needed. The same origin
serves JSON APIs. C++ owns validation, accounting, selection, valuation and persistence.
libpq connects to PostgreSQL. libxml2 handles Flex, libcurl handles fixed-host SEC HTTPS,
and SHA-256 identifies immutable source documents. No order API exists.

## Persistence and consistency

Separate entity tables store a primary ID, immutable JSONB payload and insertion timestamp.
PostgreSQL views expose typed trade and financial-history fields for SQL research.
JSONB is deliberate in v1 to retain provider metadata and allow independent adapters;
it is not a general-purpose unvalidated document store. All writes pass C++ validators.
Use the API/CLI rather than direct SQL writes. Future typed migrations can preserve IDs.

Imports run under a transaction and advisory lock, merge with current state, validate
references and replay the complete ledger before writing. Conflicting immutable IDs
fail the whole batch. Repeat records are counted, not duplicated. Import audits commit
with their records. Raw SEC JSON / Flex XML are retained locally.
All HTTP data operations use one mutex/connection; v1 targets a single local user.
This avoids concurrent use of libpq connections and simplifies deterministic validation.

## Financial calculation

Ledger quantities and amounts use signed 64-bit fixed point (six decimals) and 128-bit
intermediate multiplication. Amount limit is 9 trillion currency units; overflow fails.
Proportional basis allocations round half away from zero, with the last sale taking the
remaining lot cost exactly. Valuations, historical-FX conversions and DCF use floating
point, are estimates, and retain inputs. They are not statutory/tax accounting output.

Trades sort by date, optional numeric `sequence`, then immutable ID. Splits occur before
trades on their effective date and adjust every matching account/instrument strategy lot.
Each strategy independently matches its own FIFO lots. Buy commissions enter basis;
sell commissions reduce proceeds. Cash dividends/interest/fees/taxes are separate events.

NAV is marked securities plus cash at as-of FX. Economic P&L is NAV less signed external
flows converted at flow-date FX. Security unrealized P&L is current value less historical
base basis. Price component is local gain at current FX; FX component is local basis
times the change in FX. Cash FX revaluation and conversion differences are not assigned
to stock strategies; hence strategy P&L is not a complete account attribution waterfall.

Missing historical FX or marks suppress affected totals, not silently substitute zero.
Closed positions retain missing-history flags because their realized P&L is unknown.
Quote/FX dates cannot exceed the analysis/event date. FX forward-fill has a seven-day cap.
Multiple conflicting marks for the same instrument/date or FX currency/date are rejected.

## Fundamental research

Facts retain period start/end, period kind, unit, filed date, source, accession and tag.
An as-of query first filters disclosure dates, then selects latest annual/instant periods
within each concept/unit/kind; latest disclosure wins within period, mapping priority
breaks same-date collisions. All versions remain visible. No quarterly annualization,
synthetic TTM, Q4 derivation or segment aggregation is performed in v1.
SEC input CIK must match the instrument registration. SEC facts have a constrained
US-GAAP/DEI mapping; other markets use normalized reviewed data.

## Security and operations

Loopback bind, strict Host/Origin checking and a session token on mutations are default.
No CORS allowance. CSP disallows third-party scripts/frames. UI text is escaped; external
links permit http(s) only. Import payloads are capped at 32 MiB. XML DTD/entity inputs
are prohibited. SEC URLs are assembled from numeric CIKs; no arbitrary fetch URL or
redirect is accepted. TLS verification remains enabled.

Local OS access is the trust boundary. Workspace export includes private account data;
keep it out of Git. Do not expose the service via a tunnel or shared server without
adding authentication, authorization, encrypted transport and a multi-user data model.

## API

GET: `/api/session`, `/api/health`, `/api/instruments`, `/api/portfolio?as_of=YYYY-MM-DD`,
`/api/research?instrument=ID&as_of=DATE`, `/api/peers`, `/api/news`, `/api/data`, `/api/export`.

POST: `/api/import`, `/api/import/flex`, `/api/import/sec?instrument=ID`, `/api/sec/fetch`,
`/api/dcf`, `/api/stress`, `/api/notes`, `/api/valuations`, `/api/demo` (empty DB only).
Mutations require the `X-Reach-Token` from `/api/session`. There are no broker trade routes.

## Scaling boundary

Current imports and charts replay history in memory; the performance curve is bounded
to 2000 distinct valuation/event dates. It is for portfolios and focused research, not
global tick storage. Before larger deployment, add indexed typed projections, daily
materialized accounting snapshots and background provider jobs, verified against this engine.
