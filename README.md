# ReachTerminal

默认使用 **C++ / GTK 原生桌面前端**，不需要浏览器或 WebView。编译后运行 `./scripts/desktop.sh`；参见 [原生桌面说明](docs/native-desktop.md)。IBKR 官方网关登录与双重验证仍使用其官方外部登录流程。

An open-source, self-hosted investment research terminal. Browser frontend,
C++20 backend, PostgreSQL database. No broker order submission code.

第一版将基本面研究、策略核算、估值与风险放在同一个本地工作区。
软件不收取订阅费；第三方数据的费用、覆盖和使用权限仍由对应来源决定。

## IBKR read-only data queries

Client Portal Gateway connection settings, symbol/company search, contract details,
market snapshots and historical daily previews are available in Data Center.
Run `./scripts/gateway.sh`, sign in yourself, then use `./scripts/live.sh`.
See [IBKR setup, scope and verification](docs/ibkr.md). No generic API key is required.

## Real public data

Run `./scripts/live.sh` for the separate real-data workspace on **http://127.0.0.1:8788**.
SEC financials and US/USD raw daily OHLCV are connected. IBM works with the official
public trial; other symbols need a personal free provider key. See [setup and measured coverage](docs/live-data.md).

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
sudo apt-get install build-essential cmake ninja-build pkg-config postgresql libpq-dev \
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

## Runtime settings and shutdown

| Variable | Use |
|---|---|
| `REACH_DATABASE_URL` | PostgreSQL connection for server and CLI; setting it skips the launcher's private database setup |
| `REACH_PG_BIN` | PostgreSQL binary directory for local development setup |
| `REACH_PG_PORT` | Private development database port (default `55433`) |
| `REACH_BUILD_JOBS` | Development build parallelism (default `2`) |
| `REACH_PORT` | Browser/API port (default `8787`) |
| `REACH_ROOT` | Source/assets directory; defaults to the source path recorded at build time |
| `REACH_SEC_USER_AGENT` | Contact user agent for optional SEC fetches |

Run `./scripts/dev.sh` as a regular user. Stop the foreground server with Ctrl+C,
then run `./scripts/stop-db.sh` for the launcher's private database. This script
does not stop an external PostgreSQL instance. Stopping preserves `.local/pgdata`;
restart with `./scripts/dev.sh`. Keep the source/assets directory available to the
binary, or set `REACH_ROOT` to a directory containing `frontend/`, `db/` and `examples/`.

## Import your data

Use the Data Center or the CLI:

```sh
./build/reachterminal import examples/instrument.json
./build/reachterminal import-sec /path/to/companyfacts.json us:aapl
./build/reachterminal import-flex /path/to/flex.xml
./build/reachterminal import /path/to/normalized.json
./build/reachterminal export > /private/location/workspace.json
```

The CLI uses `REACH_DATABASE_URL` from its own shell. The development launcher sets
its connection only inside the launched process; another terminal must set it explicitly.
From the repository root, with the development database running:

```sh
export REACH_ENV=demo
export REACH_DATABASE_URL="host='$PWD/.local/socket' port=55433 dbname=reachterminal"
./build/reachterminal export > /private/location/workspace.json
```

Replace the output path with an existing private directory. If you changed
`REACH_PG_PORT`, use that port here too. Never commit account exports.
For live SEC fetching, set a real project/contact user agent before starting:

```sh
export REACH_SEC_USER_AGENT='ReachTerminal your-contact@example.org'
```

Register the instrument's `sec_cik` first. Fetches are user-triggered, single-request,
rate-limited and TLS-verified; no scraping proxy or shared API key is required.
See [data contract](docs/data-contract.md) and [architecture](docs/architecture.md).

## Important boundaries

**A working first release, not a replacement for a complete institutional data feed.**

- No TWS account stream, continuous real-time quote stream, broker margin engine,
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
python3 scripts/test-isolated.py
```

The isolated runner creates a fresh private PostgreSQL cluster, mock HTTPS Gateway and
configuration directory. Mutating tests refuse user/demo instances before any write.
Test runtimes cannot connect to real Gateway ports or external SEC/Alpha services. See [verification](docs/verification.md) for measured coverage.
Financial changes need independently computed expected values and regression cases.
Contributions for additional markets should include source terms, reproducible fixtures,
unit/period mapping and missing-data behavior. Keep provider adapters separate from accounting.

MIT applies to ReachTerminal code. Dependencies and external data retain their own licenses.
See [third-party notices](docs/third-party.md) and [roadmap](docs/roadmap.md).

## v0.1.0 扩展

新增衍生品假设对冲、IBKR 券商保证金监控、世界银行年度宏观和美联储官方新闻工作区。详见 [发布范围与验收](docs/release-0.1.0.md)。这些工作区保留数据来源和未知状态；欧式期权模型、券商保证金和现金股票账本各自计算。

## 本地内嵌发行版

前端和迁移 SQL 已内嵌到 C++ 可执行程序；不依赖运行目录里的前端资源。二进制打包与网关登录反馈见 [本地内嵌说明](docs/embedded-local.md)。
