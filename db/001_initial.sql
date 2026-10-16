BEGIN;
CREATE TABLE IF NOT EXISTS schema_versions(version integer PRIMARY KEY, applied_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE IF NOT EXISTS instruments (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS trades (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS trades_instrument_idx ON trades ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS cashflows (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS quotes (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS quotes_instrument_idx ON quotes ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS fx (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS facts (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS facts_instrument_idx ON facts ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS bars (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS bars_instrument_idx ON bars ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS news (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS news_instrument_idx ON news ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS notes (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS notes_instrument_idx ON notes ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS reconciliations (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS reconciliations_instrument_idx ON reconciliations ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS estimates (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS estimates_instrument_idx ON estimates ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS raw_sources (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS splits (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS splits_instrument_idx ON splits ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS valuations (
 id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),
 payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS valuations_instrument_idx ON valuations ((payload->>'instrument'));
CREATE TABLE IF NOT EXISTS import_runs(
 id bigserial PRIMARY KEY, created_at timestamptz NOT NULL DEFAULT now(),
 record_count integer NOT NULL, duplicate_count integer NOT NULL, collections jsonb NOT NULL
);
CREATE OR REPLACE VIEW ledger_trades AS SELECT id,payload->>'account' AS account,payload->>'instrument' AS instrument,payload->>'strategy' AS strategy,(payload->>'date')::date AS trade_date,(payload->>'quantity')::numeric AS quantity,(payload->>'price')::numeric AS price,(payload->>'fee')::numeric AS fee,payload->>'currency' AS currency FROM trades;
CREATE OR REPLACE VIEW financial_history AS SELECT id,payload->>'instrument' AS instrument,payload->>'concept' AS concept,payload->>'unit' AS unit,(payload->>'value')::numeric AS value,(payload->>'period_end')::date AS period_end,(payload->>'filed')::date AS filed,payload->>'accession' AS accession FROM facts;
INSERT INTO schema_versions(version) VALUES (1) ON CONFLICT DO NOTHING;
CREATE TABLE IF NOT EXISTS macro_observations (id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),created_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE IF NOT EXISTS hedge_models (id text PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 180),payload jsonb NOT NULL CHECK(jsonb_typeof(payload)='object' AND payload->>'id'=id),created_at timestamptz NOT NULL DEFAULT now());
INSERT INTO schema_versions(version) VALUES (2) ON CONFLICT DO NOTHING;
COMMIT;
