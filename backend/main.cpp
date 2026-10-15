#include "core.hpp"
#include "imports.hpp"
#include "market.hpp"
#include "store.hpp"
#include <filesystem>
#include <httplib.h>
#include <iostream>
#include <mutex>
#include <random>

#ifndef REACH_ROOT
#define REACH_ROOT "."
#endif
using namespace reach;
std::string env(const char *key, std::string fallback = "") {
    auto p = std::getenv(key);
    return p ? p : fallback;
}
std::string read_file(const std::string &path) {
    std::ifstream f(path);
    require(bool(f), "Cannot open file: " + path);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    require(s.size() <= 32 * 1024 * 1024, "File too large");
    return s;
}
json metrics(const json &data, const std::string &instrument, const std::string &asof) {
    auto research = fundamentals(data, instrument, asof);
    json ins;
    for (auto &i : data["instruments"])
        if (str(i, "id") == instrument)
            ins = i;
    require(!ins.is_null(), "Unknown instrument");
    auto c = str(ins, "currency");
    std::map<std::string, json> f;
    for (auto &v : research["selected"])
        if (str(v, "unit") == c || str(v, "unit") == c + "/shares" || str(v, "unit") == "shares")
            f[str(v, "concept")] = v;
    json quote;
    for (auto &q : data["quotes"])
        if (str(q, "instrument") == instrument && str(q, "date") <= asof &&
            (quote.is_null() || std::make_pair(str(q, "date"), str(q, "id")) >
                                    std::make_pair(str(quote, "date"), str(quote, "id"))))
            quote = q;
    json out = {{"price", quote.is_null() ? json(nullptr) : json(number(dec(quote, "price")))},
                {"pe", nullptr},
                {"pb", nullptr},
                {"market_cap", nullptr},
                {"fcf_yield", nullptr},
                {"net_margin", nullptr},
                {"currency", c},
                {"warnings", json::array()}};
    auto annual = [&](const char *k) {
        return f.contains(k) && str(f[k], "period_kind") == "annual";
    };
    auto aligned = [&](const char *a, const char *b) {
        return annual(a) && annual(b) && f[a]["period_start"] == f[b]["period_start"] &&
               f[a]["period_end"] == f[b]["period_end"];
    };
    auto value = [&](const char *k) { return number(dec(f.at(k), "value")); };
    if (!quote.is_null()) {
        double p = number(dec(quote, "price"));
        if (annual("eps_diluted") && value("eps_diluted") > 0)
            out["pe"] = p / value("eps_diluted");
        if (f.contains("shares") && value("shares") > 0) {
            double cap = p * value("shares");
            out["market_cap"] = cap;
            if (f.contains("equity") && value("equity") > 0)
                out["pb"] = cap / value("equity");
            if (aligned("operating_cashflow", "capex"))
                out["fcf_yield"] = (value("operating_cashflow") - value("capex")) / cap;
        }
    }
    if (aligned("revenue", "net_income") && value("revenue") > 0)
        out["net_margin"] = value("net_income") / value("revenue");
    for (auto &[k, v] : f)
        if (days(str(v, "period_end"), asof) > 450)
            out["warnings"].push_back("Stale financial input: " + k);
    out["basis"] = "Annual reported facts; market cap uses latest disclosed "
                   "shares; OCF-capex "
                   "yield is not FCFF; ratios can mix price and report dates";
    research["metrics"] = out;
    research["instrument_info"] = ins;
    research["quote"] = quote;
    if (!quote.is_null() && days(str(quote, "date"), asof) > 4)
        research["metrics"]["warnings"].push_back("Price older than four calendar days");
    for (auto kind : {"bars", "news", "notes", "valuations", "estimates"}) {
        research[kind] = json::array();
        for (auto &v : data[kind])
            if (v.value("instrument", "") == instrument &&
                v.value(kind == std::string("estimates") ? "published" : "date", asof) <= asof)
                research[kind].push_back(v);
    }
    std::sort(research["bars"].begin(), research["bars"].end(),
              [](auto &a, auto &b) { return str(a, "date") < str(b, "date"); });
    return research;
}
int main(int argc, char **argv) {
    try {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::string root = env("REACH_ROOT", REACH_ROOT),
                    dsn = env("REACH_DATABASE_URL", "dbname=reachterminal");
        Store store(dsn);
        store.migrate(root);
        std::string command = argc > 1 ? argv[1] : "serve";
        if (command == "init") {
            std::cout << "Database ready\n";
            return 0;
        }
        if (command == "import" || command == "import-flex" || command == "import-sec") {
            require(argc >= 3, "Usage: reachterminal import|import-flex FILE; "
                               "import-sec FILE INSTRUMENT");
            auto body = read_file(argv[2]);
            json batch;
            if (command == "import-flex")
                batch = flex_import(body);
            else if (command == "import-sec") {
                require(argc >= 4, "Instrument ID required");
                batch = sec_import(json::parse(body), argv[3]);
            } else
                batch = json::parse(body);
            std::cout << store.import(batch).dump(2) << '\n';
            return 0;
        }
        if (command == "export") {
            std::cout << store.load().dump(2) << '\n';
            return 0;
        }
        require(command == "serve", "Unknown command");
        httplib::Server server;
        std::mutex mutex;
        std::random_device rd;
        std::ostringstream tok;
        for (int i = 0; i < 8; ++i)
            tok << std::hex << rd();
        std::string token = tok.str();
        server.set_payload_max_length(32 * 1024 * 1024);
        server.set_read_timeout(10, 0);
        server.set_write_timeout(30, 0);
        server.set_keep_alive_max_count(20);
        std::string port = env("REACH_PORT", "8787");
        require(std::regex_match(port, std::regex("[0-9]{1,5}")) && std::stoi(port) > 0 &&
                    std::stoi(port) < 65536,
                "Invalid port");
        server.set_pre_routing_handler([&](const httplib::Request &req, httplib::Response &res) {
            auto host = req.get_header_value("Host");
            if (host != "127.0.0.1:" + port && host != "localhost:" + port) {
                res.status = 403;
                res.set_content("Invalid Host", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            auto origin = req.get_header_value("Origin");
            if (!origin.empty() && origin != "http://127.0.0.1:" + port &&
                origin != "http://localhost:" + port) {
                res.status = 403;
                res.set_content("Cross-origin requests prohibited", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.method != "GET" && req.method != "HEAD" &&
                req.get_header_value("X-Reach-Token") != token) {
                res.status = 403;
                res.set_content("Missing session token", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.set_post_routing_handler([](const auto &, auto &res) {
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("Referrer-Policy", "no-referrer");
            res.set_header("Content-Security-Policy",
                           "default-src 'self'; script-src 'self'; style-src 'self'; "
                           "img-src 'self' data:; "
                           "connect-src 'self'; frame-ancestors 'none'; object-src "
                           "'none'; base-uri 'none'");
            res.set_header("Cache-Control", "no-store");
        });
        auto route = [&](const std::string &path, bool post, auto fn) {
            auto handler = [&, fn](const httplib::Request &req, httplib::Response &res) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto result = fn(req);
                    res.set_content(result.dump(), "application/json; charset=utf-8");
                } catch (const std::exception &e) {
                    res.status = 400;
                    res.set_content(json({{"error", e.what()}}).dump(),
                                    "application/json; charset=utf-8");
                }
            };
            if (post)
                server.Post(path, handler);
            else
                server.Get(path, handler);
        };
        auto asof = [](const auto &req) {
            auto d = req.has_param("as_of") ? req.get_param_value("as_of") : today();
            date(d);
            return d;
        };
        route("/api/session", false, [&](const auto &) {
            return json{{"token", token}, {"version", "0.1.0"}, {"mode", "local-read-only-broker"}};
        });
        route("/api/health", false, [&](const auto &) {
            return json{{"ok", true}, {"database", "PostgreSQL"}, {"trading_enabled", false}};
        });
        route("/api/instruments", false, [&](const auto &) { return store.load()["instruments"]; });
        route("/api/portfolio", false,
              [&](const auto &req) { return portfolio(store.load(), asof(req), true); });
        route("/api/research", false, [&](const auto &req) {
            require(req.has_param("instrument"), "Instrument required");
            return metrics(store.load(), req.get_param_value("instrument"), asof(req));
        });
        route("/api/peers", false, [&](const auto &req) {
            json out = json::array();
            auto data = store.load();
            auto d = asof(req);
            for (auto &i : data["instruments"]) {
                auto m = metrics(data, str(i, "id"), d);
                out.push_back({{"instrument", i}, {"metrics", m["metrics"]}});
            }
            return out;
        });
        route("/api/news", false, [&](const auto &req) {
            json out = json::array();
            auto data = store.load();
            for (auto &n : data["news"])
                if (str(n, "date") <= asof(req))
                    out.push_back(n);
            std::sort(out.begin(), out.end(),
                      [](auto &a, auto &b) { return str(a, "date") > str(b, "date"); });
            return out;
        });
        route("/api/data", false, [&](const auto &) {
            auto d = store.load();
            json counts = json::object();
            bool demo = false;
            for (auto &k : collections) {
                counts[k] = d[k].size();
                for (auto &r : d[k])
                    if (r.value("source", "") == "Synthetic demo")
                        demo = true;
            }
            return json{{"counts", counts},
                        {"demo_present", demo},
                        {"imports", store.runs()},
                        {"consensus", "User-supplied licensed records only; no "
                                      "bundled global consensus feed"},
                        {"margin", "Broker margin estimation not implemented"}};
        });
        route("/api/export", false, [&](const auto &) { return store.load(); });
        route("/api/import", true,
              [&](const auto &req) { return store.import(json::parse(req.body)); });
        route("/api/demo", true, [&](const auto &) {
            auto data = store.load();
            for (auto &k : collections)
                require(data[k].empty(), "Demo can only load into an empty database; keep real and "
                                         "synthetic data separate");
            return store.import(json::parse(read_file(root + "/examples/demo.json")));
        });
        route("/api/import/flex", true,
              [&](const auto &req) { return store.import(flex_import(req.body)); });
        route("/api/import/sec", true, [&](const auto &req) {
            require(req.has_param("instrument"), "Instrument required");
            return store.import(
                sec_import(json::parse(req.body), req.get_param_value("instrument")));
        });
        std::chrono::steady_clock::time_point lastfetch{};
        route("/api/sec/fetch", true, [&](const auto &req) {
            auto v = json::parse(req.body);
            require(std::chrono::steady_clock::now() - lastfetch > std::chrono::seconds(2),
                    "Wait two seconds between SEC fetches");
            lastfetch = std::chrono::steady_clock::now();
            auto data = store.load();
            bool bound = false;
            for (auto &i : data["instruments"]) {
                require(!str(i, "id").starts_with("demo:"), "Use a separate real-data database");
                if (str(i, "id") == str(v, "instrument") && i.contains("sec_cik"))
                    bound = std::stoull(str(i, "sec_cik")) == std::stoull(str(v, "cik"));
            }
            require(bound, "Registered SEC CIK must match requested CIK");
            auto raw = json::parse(fetch_sec(str(v, "cik"), env("REACH_SEC_USER_AGENT")));
            return store.import(sec_import(raw, str(v, "instrument")));
        });
        route("/api/providers", false, [&](const auto &) {
            return json{{"sec_configured", !env("REACH_SEC_USER_AGENT").empty()},
                        {"alpha_mode",
                         env("REACH_ALPHA_API_KEY").empty() ? "IBM public trial" : "personal key"},
                        {"daily_scope", "US/USD raw daily, compact 100 sessions; not realtime"}};
        });
        std::chrono::steady_clock::time_point last_daily{};
        route("/api/market/daily", true, [&](const auto &req) {
            auto v = json::parse(req.body);
            auto data = store.load();
            for (auto &r : data["instruments"])
                require(!str(r, "id").starts_with("demo:"), "Use a separate real-data database");
            json ins;
            for (auto &i : data["instruments"])
                if (str(i, "id") == str(v, "instrument"))
                    ins = i;
            require(!ins.is_null(), "Register the instrument first");
            require(str(ins, "country") == "US" && str(ins, "currency") == "USD", "US/USD only");
            require(std::chrono::steady_clock::now() - last_daily > std::chrono::seconds(15),
                    "Wait 15 seconds between daily requests; provider daily quotas "
                    "also apply");
            last_daily = std::chrono::steady_clock::now();
            auto raw = json::parse(
                fetch_alpha_daily(str(ins, "symbol"), env("REACH_ALPHA_API_KEY", "demo")));
            auto batch = alpha_daily_import(raw, ins);
            auto result = store.import(batch);
            result["latest"] = batch["raw_sources"][0]["date"];
            return result;
        });
        route("/api/dcf", true, [&](const auto &req) { return dcf(json::parse(req.body)); });
        route("/api/stress", true, [&](const auto &req) {
            auto v = json::parse(req.body);
            return stress(portfolio(store.load(), v.value("as_of", today())), v);
        });
        route("/api/notes", true, [&](const auto &req) {
            auto v = json::parse(req.body);
            v["id"] =
                "note-" +
                hash(v.dump() +
                     std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
            return store.import(json{{"notes", json::array({v})}});
        });
        route("/api/valuations", true, [&](const auto &req) {
            auto v = json::parse(req.body);
            auto result = dcf(v.at("assumptions"));
            v["result"] = result;
            v["id"] =
                "valuation-" +
                hash(v.dump() +
                     std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
            return store.import(json{{"valuations", json::array({v})}});
        });
        require(server.set_mount_point("/", root + "/frontend"), "Frontend directory missing");
        std::cout << "ReachTerminal 0.1.0 · http://127.0.0.1:" << port
                  << " · PostgreSQL · no broker order endpoints" << std::endl;
        require(server.listen("127.0.0.1", std::stoi(port)), "Cannot bind local port");
    } catch (const std::exception &e) {
        std::cerr << "ReachTerminal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
