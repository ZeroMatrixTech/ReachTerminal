# ReachTerminal

An open-source, self-hosted investment research terminal. Browser frontend,
C++20 backend, PostgreSQL database. No broker order submission code.

第一版将基本面研究、策略核算、估值与风险放在同一个本地工作区。
软件不收取订阅费；第三方数据的费用、覆盖和使用权限仍由对应来源决定。

## What works in 0.1.0

- Six linked workspaces: portfolio, company research, valuation, risk/strategies,
  news/research notes, and data provenance/imports. Chinese UI, keyboard navigation,
  responsive layouts, interactive historical charts.
- Cash-equity ledger with strategy-scoped FIFO, fractional shares, commissions,
  deposits/withdrawals, dividends, interest/tax/fees, paired FX transfers, splits,
  historical USD cost basis, price/FX attribution, and broker snapshot reconciliation.
- Immutable, atomic, idempotent JSON imports and audited PostgreSQL persistence.
- IBKR **Flex XML file** import for the documented STK subset. No IBKR credentials needed.
- SEC CompanyFacts JSON import and optional fixed-endpoint HTTPS fetch; registered CIK
  binding, source documents, annual/instant selection, filing-date as-of views,
  restatement history. Global markets supported through normalized manual imports.
- Annual P/E, P/B, net margin and OCF-minus-capex yield with missingness and period guards.
- User-assumption FCFF DCF, sensitivity table, persisted model versions, equity/FX stress.
- Research notes, imported news links and consensus records with explicit provenance.
- Entirely synthetic global demo. No real account data, vendor data bundle or cloud service.

## Quick start (Linux)

Requires a C++20 compiler, CMake >=3.20, PostgreSQL >=14, cpp-httplib >=0.18,
nlohmann/json >=3.11, libcurl, OpenSSL and libxml2. Debian/Ubuntu packages:

```sh
sudo apt-get install build-essential cmake ninja-build postgresql libpq-dev \
  libcpp-httplib-dev nlohmann-json3-dev libcurl4-openssl-dev libssl-dev libxml2-dev
./scripts/dev.sh
```

Open **http://127.0.0.1:8787**. The development launcher creates a private PostgreSQL
cluster in `.local/`, listening on a private Unix socket, and builds the app.
It does not install system services. Use “加载合成演示数据” only in an empty database.
For real accounts use a separate cluster/database, never mix demo and real data.

On the development machine used for the initial implementation, dependency packages
are unpacked under `.local/deps/`; the launcher recognizes these without sudo.
That ignored directory is local setup, not part of the source distribution.

To use your own PostgreSQL instance:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
export REACH_DATABASE_URL='host=127.0.0.1 dbname=reachterminal user=reach'
./build/reachterminal serve
```

Create the database and role yourself first. The app applies `db/001_initial.sql`.
Stop the development database with `./scripts/stop-db.sh` after stopping the server.

## Import your data

Use the Data Center or the CLI:

```sh
./build/reachterminal import examples/instrument.json
./build/reachterminal import-sec /path/to/companyfacts.json us:aapl
./build/reachterminal import-flex /path/to/flex.xml
./build/reachterminal import /path/to/normalized.json
./build/reachterminal export > /private/location/workspace.json
```

The CLI uses the same `REACH_DATABASE_URL` as the server. Never commit account exports.
For live SEC fetching, set a real project/contact user agent before starting:

```sh
export REACH_SEC_USER_AGENT='ReachTerminal your-contact@example.org'
```

Register the instrument's `sec_cik` first. Fetches are user-triggered, single-request,
rate-limited and TLS-verified; no scraping proxy or shared API key is required.
See [data contract](docs/data-contract.md) and [architecture](docs/architecture.md).

## Important boundaries

**A working first release, not a replacement for a complete institutional data feed.**

- No TWS/Gateway live connection, real-time quote stream, broker margin engine,
  derivatives accounting, options Greeks, automatic global filings parser, full-text
  news feed or bundled global analyst-consensus history in this release.
- SEC mapping covers an explicit US-GAAP/DEI subset, not custom tags, segments or IFRS
  automation. Missing inputs stay missing. Free access is not a promise of redistribution rights.
- Only long cash equities are supported. Unsupported Flex asset classes/corporate
  actions abort the import; normalize supported splits explicitly. No partial silent skips.
- Ledger starts at zero. Import complete history or explicit documented opening cash
  and acquisition lots. Imported broker balances are **checks**, never inferred deposits.
- Strategy-scoped FIFO is an analytical attribution policy and can differ from broker
  tax-lot accounting. Trade date accounting is used, not settlement-date cash accounting.
- USD is the initial base currency; dated FX is USD per local unit, carried forward
  at most seven calendar days. Price marks are daily and flagged stale after four days.
- Return index uses an end-of-day cashflow approximation; it is not exact intraday TWR.
  No benchmark or Sharpe/VaR is invented without a suitable historical dataset.
- Stress results are model estimates, not IBKR margin requirements or liquidation thresholds.
- Bind is localhost only; Host/Origin checks, per-session write token and CSP protect
  the local app. Not designed for unauthenticated Internet hosting.

## Tests and contributions

```sh
ctest --test-dir build --output-on-failure
python3 tests/integration.py --url http://127.0.0.1:8787 --root .
```

Run integration tests against a **fresh disposable database**; they intentionally import
synthetic records. See [verification](docs/verification.md) for measured coverage.
Financial changes need independently computed expected values and regression cases.
Contributions for additional markets should include source terms, reproducible fixtures,
unit/period mapping and missing-data behavior. Keep provider adapters separate from accounting.

MIT applies to ReachTerminal code. Dependencies and external data retain their own licenses.
See [third-party notices](docs/third-party.md) and [roadmap](docs/roadmap.md).
