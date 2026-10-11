# Data contract v1

Imports are a JSON object whose keys are any subset of the collections below. Arrays
contain records with an immutable `id` (ASCII letters/digits and `_.:@/+-`, max 180).
Unknown collections fail. Same ID/same payload is idempotent; same ID/different payload
fails the entire batch. Correct a mistake by reconstructing a clean database from reviewed
exports; deletion/supersession workflows are not implemented yet. Never edit production
database rows to bypass validation.

Dates are ISO `YYYY-MM-DD`. Accounting decimals are **strings**, no exponent notation,
max six fractional digits, absolute value <=9,000,000,000,000. No implicit missing=zero.
All sourced records require `source` and `source_url`; URLs may be `https://`, `http://`,
`manual:` or `demo:`. The UI only creates clickable external links for http(s).

## Collections

| Collection | Required fields in addition to id |
| --- | --- |
| instruments | symbol, name, exchange, country, sector, currency (ISO 3 letters), asset_class=`STK`; optional sec_cik (numeric string) |
| trades | account, instrument, strategy, date, quantity (signed), price (>0), fee (>=0), currency, source, source_url; optional sequence (nonnegative integer) |
| cashflows | account, strategy, date, currency, amount (signed), kind, source, source_url |
| quotes | instrument, date, currency, price (>0), status, source, source_url |
| fx | currency, base=`USD`, date, rate (>0), source, source_url |
| splits | account, instrument, date, ratio (>0 new shares/old shares), source, source_url |
| facts | instrument, concept, value, unit, period_start, period_end, period_kind, filed, accession, taxonomy, tag, source, source_url; optional priority (integer, smaller preferred) |
| bars | instrument, date, currency, open, high, low, close, volume, source, source_url |
| reconciliations | account, date, currency, source, source_url; cash and/or instrument + quantity |
| estimates | instrument, concept, period_end, published, unit, value, analyst_count (>0 integer), statistic=`mean` or `median`, source, source_url |
| news | title, date, source, source_url; optional instrument, summary |
| notes | instrument, title, body, date |
| valuations | instrument, title, date, assumptions and exactly matching result from `/api/dcf` |
| raw_sources | date, source, source_url, document (JSON object or XML string) |

### Ledger rules

`quantity > 0` buys, `< 0` sells. Fee is always positive, charged in instrument currency.
Strategy is explicit; FIFO matches only within `(account,instrument,strategy)`. A sale
cannot consume another strategy's lots, and shorts are rejected even if aggregate account
quantity remains positive. Trade sequence matters for same-day trading; if absent, ID
lexicographic order breaks ties. Populate sequence with a consistent chronological ordering.
Strategy assignments are immutable in v1; plan tags before importing. Flex uses orderReference
when populated, otherwise `unallocated`.

Cashflow kinds: `deposit` (>0), `withdrawal` (<0), `dividend`, `interest`, `fee` (<=0),
`tax` (<=0), `fx_transfer`. Negative dividend/interest values permit reversals/debits.
`fx_transfer` requires `transfer_id` shared by exactly two opposite-signed currency legs,
same account/date. Actual executed exchange rate may differ from valuation FX; differences
remain in account economic P&L. Broker commissions must not also be imported as cash fees.

Opening ledger is zero; full history or reviewed opening cash/acquisition lots is required.
Opening cost basis and initial capital must be explicitly modeled, not inferred from a broker
snapshot. Partial imports may show negative cash or reconciliation residuals.

FX is **USD per one local unit** (EUR 1.10 means EUR 1 = USD 1.10). It is selected at/before
each transaction and valuation date, with at most seven calendar days of forward fill.
No implicit unit rate except USD. Conflicting same-currency/date FX and same-instrument/date
quotes fail rather than choosing arbitrary provider IDs. Daily price marks can be stale;
staleness is shown. Calendar/session-specific freshness policies are future work.

Splits multiply every remaining lot quantity in the account/instrument, preserving total
cost. Fractional amounts below a microshare fail. Cash-in-lieu, mergers, spinoffs, cost-basis
transfers, return of capital and rights issues require future adapters; do not fabricate trades.

### Financial facts

Canonical concepts used by the UI: revenue, net_income, operating_income, assets,
liabilities, equity, cash, operating_cashflow, capex, eps_diluted, shares,
debt_current, debt_noncurrent. Custom concepts are retained but not assumed comparable.

`period_kind`: annual (330–400 day duration), quarter (60–120 days), instant (no start),
other (retained but not fed into annual metrics). `filed` must be >= period_end.
Units are explicit, e.g. USD, EUR, shares, USD/shares. No USD/EUR or EPS/income co-aggregation.
Monetary facts are in currency units, not millions, unless an adapter converts and preserves
that provenance. Capex canonical convention is positive outflow. Report period selection
is latest end date, latest available disclosure, then tag priority; all versions are retained.

SEC imports require an instrument registered with matching `sec_cik`. The adapter maps an
explicit standard US-GAAP/DEI subset; tags and accession are preserved, unsupported custom
or IFRS tags are not automatically equated. Other markets must be reviewed into this schema.
Quarter data remains history; no TTM or inferred Q4 is displayed in v1.

Consensus is separately stored under estimates. Do not label an individual forecast as a
multi-analyst consensus. Published date controls as-of visibility. Source redistribution
rights are the importer's responsibility; the project bundles no consensus dataset.

### DCF

Assumptions: numeric fcff, growth, wacc, terminal_growth, net_debt, shares, integer years
(1–20), currency, sector. Rates are fractions (`0.10`, not `10`). FCFF and debt use the same
currency units, shares is count. Positive FCFF/shares required, WACC > terminal growth.
Sector `Financials` is excluded. All assumptions are user inputs; annual OCF-capex is not
silently substituted for unlevered FCFF. Sensitivity calculations vary WACC/terminal growth.

## IBKR Flex subset

Export **Activity Flex Query XML** with:

- Trades: STK only, EXECUTION detail, accountId, conid, symbol, description,
  listingExchange, tradeID (or transactionID), tradeDate, dateTime, quantity,
  buySell, tradePrice, currency, ibCommission, ibCommissionCurrency, orderReference.
- CashTransactions: transactionID, type, amount, currency, dateTime/reportDate.
  Supported types are Deposits/Withdrawals, Dividends, Payment In Lieu Of Dividends,
  Withholding Tax, Broker Interest Received/Paid and Other Fees.
- OpenPositions SUMMARY: conid, position, currency, reportDate (or statement toDate).
- CashReportCurrency: currency and endingCash; BASE_SUMMARY is not a native-currency balance.

Trade and commission currencies must match. Buy/sell direction must match signed quantity.
Unsupported trades/cash transaction types/corporate actions reject the file atomically.
Summary duplicates, corrections or fields outside this contract need normalization before
import. FX executions are not automatically translated from CASH trades in this release.
Raw XML is retained. No user Flex token is stored or sent; this is a file adapter, not a
Flex Web Service polling integration. See synthetic fixture `examples/flex-sample.xml`.
