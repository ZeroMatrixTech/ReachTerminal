#pragma once
#include "core.hpp"
#include "embedded_assets.hpp"
#include <fstream>
#include <libpq-fe.h>
#include <memory>

namespace reach {
class Store {
    std::unique_ptr<PGconn, decltype(&PQfinish)> conn;
    using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
    Result query(const std::string &sql, const std::vector<std::string> &params = {}) {
        std::vector<const char *> p;
        for (auto &s : params)
            p.push_back(s.c_str());
        Result r(params.empty() ? PQexec(conn.get(), sql.c_str())
                                : PQexecParams(conn.get(), sql.c_str(), static_cast<int>(p.size()),
                                               nullptr, p.data(), nullptr, nullptr, 0),
                 PQclear);
        require(bool(r), "Database query failed");
        auto status = PQresultStatus(r.get());
        require(status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK,
                std::string("Database operation failed: ") + PQresultErrorMessage(r.get()));
        return r;
    }

  public:
    explicit Store(const std::string &dsn) : conn(PQconnectdb(dsn.c_str()), PQfinish) {
        require(conn && PQstatus(conn.get()) == CONNECTION_OK,
                "PostgreSQL connection failed; check REACH_DATABASE_URL");
    }
    std::string database_name() const {
        return PQdb(conn.get());
    }
    void migrate(const std::string &) {
        query(std::string(assets::schema()));
    }
    json load() {
        bool own_transaction = PQtransactionStatus(conn.get()) == PQTRANS_IDLE;
        if (own_transaction)
            query("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
        try {
            json d = empty_data();
            for (auto &k : collections) {
                auto r = query("SELECT payload::text FROM " + k + " ORDER BY id");
                for (int i = 0; i < PQntuples(r.get()); ++i)
                    d[k].push_back(json::parse(PQgetvalue(r.get(), i, 0)));
            }
            if (own_transaction)
                query("COMMIT");
            return d;
        } catch (...) {
            if (own_transaction) {
                auto r = PQexec(conn.get(), "ROLLBACK");
                if (r)
                    PQclear(r);
            }
            throw;
        }
    }
    json import(const json &batch) {
        query("BEGIN ISOLATION LEVEL READ COMMITTED");
        try {
            query("SELECT pg_advisory_xact_lock(736283192)");
            auto current = load();
            auto merged = merge_data(current, batch);
            // Replay the complete ledger before any write: reject oversells even when importing old
            // events.
            portfolio(merged, "2200-12-31");
            int inserted = 0, duplicates = 0;
            for (auto &k : collections)
                if (batch.contains(k))
                    for (auto &v : batch[k]) {
                        auto r = query("INSERT INTO " + k +
                                           "(id,payload) VALUES ($1,$2::jsonb) ON CONFLICT(id) DO "
                                           "NOTHING RETURNING id",
                                       {str(v, "id"), v.dump()});
                        if (PQntuples(r.get()))
                            ++inserted;
                        else
                            ++duplicates;
                    }
            query("INSERT INTO import_runs(record_count,duplicate_count,collections) VALUES "
                  "($1::int,$2::int,$3::jsonb)",
                  {std::to_string(inserted), std::to_string(duplicates), [&] {
                       json a = json::array();
                       for (auto it = batch.begin(); it != batch.end(); ++it)
                           a.push_back(it.key());
                       return a.dump();
                   }()});
            query("COMMIT");
            return {{"inserted", inserted}, {"duplicates", duplicates}};
        } catch (...) {
            auto rollback = PQexec(conn.get(), "ROLLBACK");
            if (rollback)
                PQclear(rollback);
            throw;
        }
    }
    json runs() {
        auto r = query("SELECT id,created_at::text,record_count,duplicate_count FROM import_runs "
                       "ORDER BY id DESC LIMIT 20");
        json out = json::array();
        for (int i = 0; i < PQntuples(r.get()); ++i)
            out.push_back({{"id", PQgetvalue(r.get(), i, 0)},
                           {"created_at", PQgetvalue(r.get(), i, 1)},
                           {"inserted", std::stoi(PQgetvalue(r.get(), i, 2))},
                           {"duplicates", std::stoi(PQgetvalue(r.get(), i, 3))}});
        return out;
    }
};
} // namespace reach
