# Roadmap

0.1 establishes the cash-equity research workflow and explicit data boundaries.
Next stages, in dependency order:

1. Reviewed IBKR statement fixtures from volunteers, settlement/tax-lot reconciliation,
   transaction corrections with supersession audit, fuller corporate actions and FX executions.
2. European ESEF and other market adapters, entity/listing identifiers, financial-company
   models, currencies and units, reliable point-in-time historical datasets.
3. Read-only TWS/IB Gateway connector with user-owned entitlements, connection state,
   daily snapshots, alerts and event-driven refresh. Verify API/SDK licensing separately.
4. Suitable benchmark datasets, precise flow-timed returns, factor attribution, covariance
   and VaR/ES with model diagnostics, rather than metrics on arbitrary missing histories.
5. Derivative contract model, QuantLib evaluation, option-chain data adapters, Greeks,
   scenario full repricing, hedge comparison and financing cost accounting.
6. Broker-reported margin monitoring, separately labeled internal estimates, model/version
   validation and stress liquidity analysis. No claim to replicate broker house rules.

Free default operation remains mandatory. Paid providers may only be optional user-configured
adapters; core features must not require a paid shared key or hosted service. No trading routes.
