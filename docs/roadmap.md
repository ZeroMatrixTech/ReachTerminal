# Roadmap

0.1 includes cash-equity research/accounting, device-bound Client Portal reads, European modeled hedges, broker margin totals, annual WDI macro vintages and official Fed RSS. See release-0.1.0.md for exact coverage.
Next stages, in dependency order:

1. Reviewed IBKR statement fixtures from volunteers, settlement/tax-lot reconciliation,
   transaction corrections with supersession audit, fuller corporate actions and FX executions.
2. European ESEF and other market adapters, entity/listing identifiers, financial-company
   models, currencies and units, reliable point-in-time historical datasets.
3. Extend the existing Client Portal connector with optional TWS/IB Gateway, user-owned entitlements, persisted market snapshots and event-driven refresh. Verify API/SDK licensing separately.
4. Suitable benchmark datasets, precise flow-timed returns, factor attribution, covariance
   and VaR/ES with model diagnostics, rather than metrics on arbitrary missing histories.
5. Extend the European hedge scenario model with contract reference data, option chains, American/exotic pricing, derivatives transaction accounting and financing costs.
6. Extend broker margin monitoring with threshold alerts, stress liquidity analysis and separately validated internal estimates. No claim to replicate broker house rules.

Free default operation remains mandatory. Paid providers may only be optional user-configured
adapters; core features must not require a paid shared key or hosted service. No trading routes.
