#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iomanip>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace reach {
using json = nlohmann::json;
using Money = int64_t;
constexpr Money scale = 1000000;
inline void require(bool b, const std::string &message) {
    if (!b)
        throw std::runtime_error(message);
}
inline std::string str(const json &j, const char *k) {
    require(j.contains(k) && j.at(k).is_string(), std::string("Required string: ") + k);
    auto s = j.at(k).get<std::string>();
    require(!s.empty() && s.size() <= 2000, "Invalid string length");
    return s;
}
inline Money decimal(const std::string &s) {
    require(std::regex_match(s, std::regex(R"(-?[0-9]{1,13}(\.[0-9]{1,6})?)")),
            "Decimal must be a string, <=6 decimal places: " + s);
    bool neg = s[0] == '-';
    auto p = s.find('.');
    auto whole = s.substr(neg ? 1 : 0, p == std::string::npos ? p : p - (neg ? 1 : 0));
    auto frac = p == std::string::npos ? std::string() : s.substr(p + 1);
    frac.append(6 - frac.size(), '0');
    __int128 n = static_cast<__int128>(std::stoll(whole)) * scale + std::stoll(frac);
    require(n <= 9000000000000000000LL, "Decimal out of range");
    return static_cast<Money>(neg ? -n : n);
}
inline Money dec(const json &j, const char *k) {
    return decimal(str(j, k));
}
inline double number(Money n) {
    return static_cast<double>(n) / scale;
}
inline Money checked(__int128 v) {
    require(v <= 9000000000000000000LL && v >= -9000000000000000000LL, "Accounting overflow");
    return static_cast<Money>(v);
}
inline Money add(Money a, Money b) {
    return checked(static_cast<__int128>(a) + b);
}
inline Money prorate(Money amount, Money part, Money total) {
    require(total > 0, "Invalid divisor");
    __int128 n = static_cast<__int128>(amount) * part;
    bool neg = n < 0;
    if (neg)
        n = -n;
    return checked((neg ? -1 : 1) * ((n + total / 2) / total));
}
inline Money product(Money a, Money b) {
    return prorate(a, b, scale);
}
inline std::chrono::sys_days date(const std::string &s) {
    require(std::regex_match(s, std::regex(R"([0-9]{4}-[0-9]{2}-[0-9]{2})")),
            "Expected YYYY-MM-DD");
    int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    auto v = std::chrono::year(y) / std::chrono::month(m) / std::chrono::day(d);
    require(v.ok() && y >= 1900 && y <= 2200, "Invalid date");
    return std::chrono::sys_days(v);
}
inline int days(const std::string &a, const std::string &b) {
    return static_cast<int>((date(b) - date(a)).count());
}
inline std::string today() {
    auto d = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
    std::ostringstream s;
    s << int(d.year()) << '-' << std::setw(2) << std::setfill('0') << unsigned(d.month()) << '-'
      << std::setw(2) << unsigned(d.day());
    return s.str();
}
inline void id(const std::string &s) {
    require(std::regex_match(s, std::regex(R"([A-Za-z0-9_.:@/+-]{1,180})")), "Invalid ID");
}
inline void currency(const std::string &s) {
    require(std::regex_match(s, std::regex("[A-Z]{3}")), "Invalid currency");
}
inline void source(const json &v) {
    str(v, "source");
    auto u = str(v, "source_url");
    require(u.starts_with("https://") || u.starts_with("http://") || u.starts_with("manual:") ||
                u.starts_with("demo:"),
            "Source URL must use http(s), manual: or demo:");
}
inline std::string retrieval_date(int64_t ms) {
    require(ms > 0 && ms <= 4102444800000LL, "Invalid retrieval epoch");
    std::time_t t = ms / 1000;
    std::tm tm{};
    require(gmtime_r(&t, &tm) != nullptr, "Invalid retrieval timestamp");
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}
inline const std::vector<std::string> collections = {
    "instruments", "trades",      "cashflows", "quotes",     "fx",
    "facts",       "bars",        "news",      "notes",      "reconciliations",
    "estimates",   "raw_sources", "splits",    "valuations", "macro_observations",
    "hedge_models"};
inline json empty_data() {
    json j = json::object();
    for (auto &k : collections)
        j[k] = json::array();
    return j;
}
inline json dcf(const json &v);
inline json hedge_book(const json &v);
inline void validate(const std::string &kind, const json &v) {
    require(v.is_object(), "Record must be object");
    id(str(v, "id"));
    if (kind == "instruments") {
        for (auto k :
             {"symbol", "name", "exchange", "country", "sector", "currency", "asset_class"})
            str(v, k);
        currency(str(v, "currency"));
        require(str(v, "asset_class") == "STK", "v1 accounting supports cash equities only");
        if (v.contains("sec_cik") && !str(v, "sec_cik").empty())
            require(std::regex_match(str(v, "sec_cik"), std::regex("[0-9]{1,10}")),
                    "Invalid registered CIK");
    } else if (kind == "trades") {
        for (auto k : {"account", "instrument", "strategy"})
            id(str(v, k));
        date(str(v, "date"));
        currency(str(v, "currency"));
        require(dec(v, "quantity") != 0 && dec(v, "price") > 0 && dec(v, "fee") >= 0,
                "Invalid trade quantity/price/fee");
        if (v.contains("sequence"))
            require(v["sequence"].is_number_integer() && v["sequence"].get<int>() >= 0,
                    "Invalid trade sequence");
        source(v);
    } else if (kind == "cashflows") {
        id(str(v, "account"));
        id(str(v, "strategy"));
        date(str(v, "date"));
        currency(str(v, "currency"));
        auto k = str(v, "kind");
        std::set<std::string> allowed = {"deposit", "withdrawal", "dividend",   "interest",
                                         "fee",     "tax",        "fx_transfer"};
        require(allowed.contains(k),
                "Unsupported cash event; corporate actions require explicit handling");
        auto a = dec(v, "amount");
        require((k != "deposit" || a > 0) && (k != "withdrawal" || a < 0) &&
                    ((k != "fee" && k != "tax") || a <= 0),
                "Cash event sign mismatch");
        if (k == "fx_transfer")
            id(str(v, "transfer_id"));
        source(v);
    } else if (kind == "quotes" || kind == "bars") {
        id(str(v, "instrument"));
        date(str(v, "date"));
        currency(str(v, "currency"));
        source(v);
        if (kind == "quotes") {
            require(dec(v, "price") > 0, "Invalid quote");
            auto s = str(v, "status");
            require(s == "eod" || s == "delayed" || s == "realtime" || s == "manual" || s == "demo",
                    "Invalid quote status");
        } else {
            for (auto k : {"open", "high", "low", "close"})
                require(dec(v, k) > 0, "Invalid OHLC");
            require(dec(v, "volume") >= 0, "Invalid volume");
            require(dec(v, "low") <= std::min(dec(v, "open"), dec(v, "close")) &&
                        dec(v, "high") >= std::max(dec(v, "open"), dec(v, "close")) &&
                        dec(v, "low") <= dec(v, "high"),
                    "Invalid OHLC range");
        }
    } else if (kind == "fx") {
        currency(str(v, "currency"));
        require(str(v, "base") == "USD", "v1 base currency is USD");
        date(str(v, "date"));
        require(dec(v, "rate") > 0, "FX rate must be positive USD per local unit");
        source(v);
    } else if (kind == "facts") {
        id(str(v, "instrument"));
        for (auto k : {"concept", "unit", "taxonomy", "tag", "accession", "period_kind"})
            str(v, k);
        date(str(v, "period_end"));
        date(str(v, "filed"));
        require(str(v, "filed") >= str(v, "period_end"), "Fact filed before period end");
        dec(v, "value");
        if (str(v, "concept") == "capex")
            require(dec(v, "value") >= 0, "Canonical capex is a positive outflow");
        if (v.contains("priority"))
            require(v["priority"].is_number_integer() && v["priority"].get<int>() >= 0 &&
                        v["priority"].get<int>() <= 10000,
                    "Invalid mapping priority");
        source(v);
        auto k = str(v, "period_kind");
        require(k == "annual" || k == "quarter" || k == "instant" || k == "other",
                "Invalid period kind");
        if (k != "instant") {
            int n = days(str(v, "period_start"), str(v, "period_end"));
            require(n >= 0, "Negative duration");
            require(k != "annual" || (n >= 330 && n <= 400), "Annual duration mismatch");
            require(k != "quarter" || (n >= 60 && n <= 120), "Quarter duration mismatch");
        } else
            require(v.value("period_start", "").empty(), "Instant fact cannot have start");
    } else if (kind == "splits") {
        id(str(v, "instrument"));
        id(str(v, "account"));
        date(str(v, "date"));
        require(dec(v, "ratio") > 0, "Invalid split ratio");
        source(v);
    } else if (kind == "reconciliations") {
        id(str(v, "account"));
        date(str(v, "date"));
        currency(str(v, "currency"));
        require(v.contains("cash") || v.contains("quantity"),
                "Reconciliation needs cash or quantity");
        if (v.contains("cash"))
            dec(v, "cash");
        if (v.contains("quantity")) {
            id(str(v, "instrument"));
            dec(v, "quantity");
        }
        source(v);
    } else if (kind == "estimates") {
        id(str(v, "instrument"));
        date(str(v, "published"));
        date(str(v, "period_end"));
        str(v, "concept");
        str(v, "unit");
        dec(v, "value");
        source(v);
        require(v.contains("analyst_count") && v["analyst_count"].is_number_integer() &&
                    v["analyst_count"].get<int>() > 0,
                "Consensus requires analyst_count");
        require(str(v, "statistic") == "mean" || str(v, "statistic") == "median",
                "Consensus statistic required");
    } else if (kind == "news") {
        str(v, "title");
        date(str(v, "date"));
        source(v);
        if (v.contains("instrument"))
            id(str(v, "instrument"));
    } else if (kind == "notes") {
        id(str(v, "instrument"));
        str(v, "title");
        str(v, "body");
        date(str(v, "date"));
    } else if (kind == "raw_sources") {
        source(v);
        date(str(v, "date"));
        require(v.contains("document"), "Raw document required");
    } else if (kind == "macro_observations") {
        date(str(v, "date"));
        date(str(v, "period_end"));
        source(v);
        require(str(v, "period_end") <= str(v, "date"), "Macro period is in future");
        require(std::regex_match(str(v, "country"), std::regex("[A-Z]{3}")),
                "Invalid macro country");
        auto indicator = str(v, "indicator");
        require(indicator == "gdp" || indicator == "growth" || indicator == "inflation",
                "Invalid macro indicator");
        require(str(v, "frequency") == "annual" &&
                    str(v, "unit") == (indicator == "gdp" ? "current USD" : "percent"),
                "Invalid macro unit/frequency");
        require(v.contains("known_at") && v["known_at"].is_null(),
                "Historical release date unavailable");
        require(v.contains("retrieved_at_ms") && v["retrieved_at_ms"].is_number_integer() &&
                    v["retrieved_at_ms"].get<int64_t>() > 0,
                "Invalid macro retrieval timestamp");
        require(str(v, "date") == retrieval_date(v["retrieved_at_ms"].get<int64_t>()) &&
                    str(v, "date") <= today(),
                "Macro visibility must match actual retrieval date");
        require(v.contains("value") &&
                    (v["value"].is_null() ||
                     (v["value"].is_number() && std::isfinite(v["value"].get<double>()) &&
                      std::abs(v["value"].get<double>()) <= 1e18)),
                "Invalid macro value");
    } else if (kind == "hedge_models") {
        date(str(v, "date"));
        str(v, "title");
        require(v.at("result") == hedge_book(v.at("assumptions")),
                "Hedge result differs from assumptions");
    } else if (kind == "valuations") {
        id(str(v, "instrument"));
        date(str(v, "date"));
        str(v, "title");
        require(v.contains("assumptions") && v["assumptions"].is_object(),
                "Valuation assumptions required");
        require(v.contains("result") && v["result"] == dcf(v["assumptions"]),
                "Valuation result must match recorded assumptions");
    }
}

inline json merge_data(json current, const json &batch) {
    require(batch.is_object(), "Import must be a JSON object");
    for (auto it = batch.begin(); it != batch.end(); ++it)
        require(std::find(collections.begin(), collections.end(), it.key()) != collections.end(),
                "Unknown collection: " + it.key());
    for (auto &kind : collections) {
        if (!batch.contains(kind))
            continue;
        require(batch[kind].is_array(), "Collection must be an array: " + kind);
        std::map<std::string, json> existing;
        for (auto &v : current[kind])
            existing[str(v, "id")] = v;
        for (auto &v : batch[kind]) {
            validate(kind, v);
            auto k = str(v, "id");
            if (existing.contains(k))
                require(existing[k] == v, "Immutable ID collision: " + kind + "/" + k);
            else {
                existing[k] = v;
                current[kind].push_back(v);
            }
        }
    }
    std::map<std::string, json> ins;
    for (auto &i : current["instruments"])
        ins[str(i, "id")] = i;
    for (auto kind :
         {"trades", "quotes", "bars", "facts", "splits", "notes", "estimates", "valuations"})
        for (auto &v : current[kind]) {
            auto k = str(v, "instrument");
            require(ins.contains(k), "Unknown instrument: " + k);
            if (v.contains("currency"))
                require(str(v, "currency") == str(ins[k], "currency"),
                        "Instrument currency mismatch");
            if (v.value("source", "") == "SEC CompanyFacts")
                require(ins[k].contains("sec_cik") && std::stoull(str(ins[k], "sec_cik")) ==
                                                          std::stoull(str(v, "entity_cik")),
                        "SEC CIK does not match instrument registration; register sec_cik before "
                        "importing");
        }
    std::map<std::string, json> fxkeys, quotekeys;
    std::set<std::string> splitkeys;
    for (auto &s : current["splits"])
        require(
            splitkeys.insert(str(s, "account") + "|" + str(s, "instrument") + "|" + str(s, "date"))
                .second,
            "Duplicate split for account/instrument/date");
    for (auto &f : current["fx"]) {
        if (str(f, "currency") == "USD")
            require(dec(f, "rate") == scale, "USD base FX must equal one");
        auto k = str(f, "currency") + "|" + str(f, "date");
        if (fxkeys.contains(k))
            require(dec(fxkeys[k], "rate") == dec(f, "rate"),
                    "Ambiguous FX for currency/date; use a single authoritative rate");
        fxkeys[k] = f;
    }
    for (auto &q : current["quotes"]) {
        auto k = str(q, "instrument") + "|" + str(q, "date");
        if (quotekeys.contains(k))
            require(dec(quotekeys[k], "price") == dec(q, "price") &&
                        str(quotekeys[k], "status") == str(q, "status"),
                    "Ambiguous quote for instrument/date; v1 stores one authoritative daily mark");
        quotekeys[k] = q;
    }
    std::map<std::string, std::vector<json>> transfers;
    for (auto &c : current["cashflows"])
        if (str(c, "kind") == "fx_transfer")
            transfers[str(c, "transfer_id")].push_back(c);
    for (auto &[_, v] : transfers)
        require(v.size() == 2 && str(v[0], "account") == str(v[1], "account") &&
                    str(v[0], "date") == str(v[1], "date") &&
                    str(v[0], "currency") != str(v[1], "currency") &&
                    ((dec(v[0], "amount") > 0) != (dec(v[1], "amount") > 0)),
                "FX transfer must contain two opposite currency legs on same account/date");
    return current;
}

struct Lot {
    Money qty, cost;
    long double base_cost;
};
struct Position {
    std::deque<Lot> lots;
    Money qty = 0, realized = 0;
    long double realized_base = 0;
    bool base_complete = true;
};
inline json portfolio(const json &data, const std::string &asof, bool include_curve = false) {
    date(asof);
    std::map<std::string, json> instruments, marks;
    std::map<std::string, std::vector<json>> rates;
    for (auto &i : data["instruments"])
        instruments[str(i, "id")] = i;
    for (auto &f : data["fx"])
        if (str(f, "date") <= asof)
            rates[str(f, "currency")].push_back(f);
    auto fx = [&](const std::string &c, const std::string &d) -> long double {
        if (c == "USD")
            return 1;
        json best;
        for (auto &f : rates[c])
            if (str(f, "date") <= d &&
                (best.is_null() || str(f, "date") > str(best, "date") ||
                 (str(f, "date") == str(best, "date") && str(f, "id") > str(best, "id"))))
                best = f;
        return best.is_null() || days(str(best, "date"), d) > 7
                   ? NAN
                   : static_cast<long double>(dec(best, "rate")) / scale;
    };
    for (auto &q : data["quotes"])
        if (str(q, "date") <= asof) {
            auto k = str(q, "instrument");
            if (!marks.contains(k) ||
                std::make_pair(str(q, "date"), str(q, "id")) >
                    std::make_pair(str(marks[k], "date"), str(marks[k], "id")))
                marks[k] = q;
        }
    std::vector<json> events;
    for (auto k : {"trades", "splits"})
        for (auto v : data[k])
            if (str(v, "date") <= asof) {
                v["_type"] = k;
                events.push_back(v);
            }
    std::sort(events.begin(), events.end(), [](auto &a, auto &b) {
        return std::make_tuple(str(a, "date"), a["_type"] == "splits" ? 0 : 1,
                               a.value("sequence", 0), str(a, "id")) <
               std::make_tuple(str(b, "date"), b["_type"] == "splits" ? 0 : 1,
                               b.value("sequence", 0), str(b, "id"));
    });
    using Key = std::tuple<std::string, std::string, std::string>;
    std::map<Key, Position> positions;
    std::map<std::pair<std::string, std::string>, Money> cash;
    std::map<std::string, long double> incomes;
    std::map<std::string, bool> strategy_complete;
    json warnings = json::array();
    long double external = 0;
    bool complete = true, external_complete = true;
    for (auto &e : events) {
        auto a = str(e, "account"), i = str(e, "instrument");
        if (e["_type"] == "splits") {
            for (auto &[k, p] : positions)
                if (std::get<0>(k) == a && std::get<1>(k) == i) {
                    p.qty = 0;
                    for (auto &l : p.lots) {
                        auto n = static_cast<__int128>(l.qty) * dec(e, "ratio");
                        require(n % scale == 0, "Split creates fractional microshare; import "
                                                "explicit corporate-action reconciliation");
                        l.qty = checked(n / scale);
                        p.qty = add(p.qty, l.qty);
                    }
                }
            continue;
        }
        auto s = str(e, "strategy"), c = str(e, "currency"), d = str(e, "date");
        Key k = {a, i, s};
        auto &p = positions[k];
        auto q = dec(e, "quantity"), fee = dec(e, "fee"), gross = product(q, dec(e, "price"));
        auto rate = fx(c, d);
        cash[{a, c}] = add(cash[{a, c}], add(-gross, -fee));
        if (!std::isfinite(rate)) {
            p.base_complete = false;
            strategy_complete[s] = false;
            complete = false;
        }
        if (q > 0) {
            auto cost = add(gross, fee);
            p.lots.push_back({q, cost, std::isfinite(rate) ? number(cost) * rate : 0});
            p.qty = add(p.qty, q);
        } else {
            Money remaining = -q;
            require(p.qty >= remaining, "Sell exceeds strategy long position: " + a + "/" + i +
                                            "/" + s +
                                            "; no shorts or cross-strategy matching in v1");
            Money cost = 0;
            long double basecost = 0;
            while (remaining > 0) {
                auto &l = p.lots.front();
                auto take = std::min(remaining, l.qty);
                auto part = take == l.qty ? l.cost : prorate(l.cost, take, l.qty);
                auto bp = l.base_cost * static_cast<long double>(take) / l.qty;
                cost = add(cost, part);
                basecost += bp;
                l.cost -= part;
                l.base_cost -= bp;
                l.qty -= take;
                remaining -= take;
                if (!l.qty)
                    p.lots.pop_front();
            }
            auto proceeds = add(-gross, -fee);
            p.realized = add(p.realized, add(proceeds, -cost));
            if (std::isfinite(rate))
                p.realized_base += number(proceeds) * rate - basecost;
            p.qty = add(p.qty, q);
        }
    }
    for (auto &e : data["cashflows"])
        if (str(e, "date") <= asof) {
            auto a = str(e, "account"), c = str(e, "currency"), s = str(e, "strategy"),
                 kind = str(e, "kind");
            auto n = dec(e, "amount");
            cash[{a, c}] = add(cash[{a, c}], n);
            auto f = fx(c, str(e, "date"));
            if (kind == "deposit" || kind == "withdrawal") {
                if (std::isfinite(f))
                    external += number(n) * f;
                else
                    external_complete = false;
            } else if (kind != "fx_transfer") {
                if (std::isfinite(f))
                    incomes[s] += number(n) * f;
                else {
                    strategy_complete[s] = false;
                    complete = false;
                }
            }
        }
    json rows = json::array(), cashrows = json::array(), strategies = json::object(),
         currency_exposure = json::object(), sector_exposure = json::object();
    long double market = 0, cashbase = 0, realized = 0, unrealized = 0;
    for (auto &[k, p] : positions) {
        auto [a, i, s] = k;
        auto &in = instruments.at(i);
        auto c = str(in, "currency");
        Money cost = 0;
        long double bc = 0;
        for (auto &l : p.lots) {
            cost = add(cost, l.cost);
            bc += l.base_cost;
        }
        auto f = fx(c, asof);
        bool markok = marks.contains(i) && std::isfinite(f);
        bool ok = p.base_complete && (p.qty == 0 || markok);
        json row = {{"account", a},
                    {"instrument", i},
                    {"strategy", s},
                    {"symbol", in["symbol"]},
                    {"name", in["name"]},
                    {"currency", c},
                    {"sector", in["sector"]},
                    {"quantity", number(p.qty)},
                    {"cost_local", number(cost)},
                    {"realized_local", number(p.realized)},
                    {"realized_base",
                     p.base_complete ? json(static_cast<double>(p.realized_base)) : json(nullptr)},
                    {"market_value_base", nullptr},
                    {"unrealized_base", nullptr},
                    {"price_pnl_base", nullptr},
                    {"fx_pnl_base", nullptr}};
        long double mv = 0, up = 0;
        if (p.qty && markok) {
            auto &m = marks[i];
            mv = number(product(p.qty, dec(m, "price"))) * f;
            up = mv - bc;
            row["price"] = number(dec(m, "price"));
            row["quote_date"] = m["date"];
            row["quote_status"] = m["status"];
            row["market_value_base"] = static_cast<double>(mv);
            if (p.base_complete) {
                row["unrealized_base"] = static_cast<double>(up);
                row["price_pnl_base"] = static_cast<double>(
                    (number(product(p.qty, dec(m, "price"))) - number(cost)) * f);
                row["fx_pnl_base"] = static_cast<double>(number(cost) * f - bc);
            }
            if (days(str(m, "date"), asof) > 4)
                warnings.push_back("Stale quote: " + str(in, "symbol") + " " + str(m, "date"));
        }
        if (p.qty && !markok) {
            complete = false;
            warnings.push_back("Missing mark or FX: " + str(in, "symbol"));
        }
        if (!p.base_complete) {
            complete = false;
            warnings.push_back("Missing historical FX: " + str(in, "symbol"));
        }
        market += mv;
        realized += p.realized_base;
        unrealized += up;
        rows.push_back(row);
        if (!strategies.contains(s))
            strategies[s] = {{"name", s},
                             {"market_value_base", 0.0},
                             {"realized_base", 0.0},
                             {"unrealized_base", 0.0},
                             {"income_base", 0.0},
                             {"complete", true}};
        auto &st = strategies[s];
        st["market_value_base"] = st["market_value_base"].get<double>() + static_cast<double>(mv);
        st["realized_base"] =
            st["realized_base"].get<double>() + static_cast<double>(p.realized_base);
        st["unrealized_base"] = st["unrealized_base"].get<double>() + static_cast<double>(up);
        if (!ok)
            st["complete"] = false;
        currency_exposure[c] = currency_exposure.value(c, 0.0) + static_cast<double>(mv);
        auto sector = str(in, "sector");
        sector_exposure[sector] = sector_exposure.value(sector, 0.0) + static_cast<double>(mv);
    }
    for (auto &[s, n] : incomes) {
        if (!strategies.contains(s))
            strategies[s] = {{"name", s},
                             {"market_value_base", 0.0},
                             {"realized_base", 0.0},
                             {"unrealized_base", 0.0},
                             {"income_base", 0.0},
                             {"complete", true}};
        strategies[s]["income_base"] = static_cast<double>(n);
    }
    for (auto &[s, ok] : strategy_complete)
        if (!ok && strategies.contains(s))
            strategies[s]["complete"] = false;
    for (auto &[k, n] : cash) {
        auto [a, c] = k;
        auto f = fx(c, asof);
        bool ok = n == 0 || std::isfinite(f);
        if (n == 0)
            f = 0;
        if (!ok) {
            complete = false;
            warnings.push_back("Missing cash FX: " + c);
        } else
            cashbase += number(n) * f;
        cashrows.push_back(
            {{"account", a},
             {"currency", c},
             {"amount", number(n)},
             {"base_value", ok ? json(number(n) * static_cast<double>(f)) : json(nullptr)}});
        if (n < 0)
            warnings.push_back("Negative cash: " + a + " " + c +
                               "; financing interest must be imported separately");
    }
    json strategyrows = json::array();
    for (auto &[_, v] : strategies.items()) {
        if (v["complete"] == true)
            v["pnl_base"] = v["realized_base"].get<double>() + v["unrealized_base"].get<double>() +
                            v["income_base"].get<double>();
        else {
            v["pnl_base"] = nullptr;
            v["realized_base"] = nullptr;
            v["unrealized_base"] = nullptr;
        }
        strategyrows.push_back(v);
    }
    json reconciliation = json::array();
    for (auto &r : data["reconciliations"])
        if (str(r, "date") == asof) {
            json v = r;
            if (r.contains("cash"))
                v["cash_difference"] =
                    number(add(cash[{str(r, "account"), str(r, "currency")}], -dec(r, "cash")));
            if (r.contains("quantity")) {
                Money q = 0;
                for (auto &[k, p] : positions)
                    if (std::get<0>(k) == str(r, "account") &&
                        std::get<1>(k) == str(r, "instrument"))
                        q = add(q, p.qty);
                v["quantity_difference"] = number(add(q, -dec(r, "quantity")));
            }
            reconciliation.push_back(v);
        }
    long double nav = market + cashbase;
    json result = {
        {"as_of", asof},
        {"base_currency", "USD"},
        {"complete", complete && external_complete},
        {"nav", complete ? json(static_cast<double>(nav)) : json(nullptr)},
        {"market_value", complete ? json(static_cast<double>(market)) : json(nullptr)},
        {"cash_value", complete ? json(static_cast<double>(cashbase)) : json(nullptr)},
        {"net_external_flows",
         external_complete ? json(static_cast<double>(external)) : json(nullptr)},
        {"economic_pnl",
         complete && external_complete ? json(static_cast<double>(nav - external)) : json(nullptr)},
        {"realized_base", complete ? json(static_cast<double>(realized)) : json(nullptr)},
        {"unrealized_base", complete ? json(static_cast<double>(unrealized)) : json(nullptr)},
        {"positions", rows},
        {"cash", cashrows},
        {"strategies", strategyrows},
        {"warnings", warnings},
        {"currency_exposure", currency_exposure},
        {"sector_exposure", sector_exposure},
        {"reconciliation", reconciliation},
        {"scenario_model",
         "Long cash equities, linear price/FX shocks; no option or broker margin model"}};
    if (include_curve) {
        std::set<std::string> dates;
        for (auto kind : {"quotes", "cashflows", "trades", "splits"})
            for (auto &r : data[kind])
                if (str(r, "date") <= asof)
                    dates.insert(str(r, "date"));
        require(dates.size() <= 2000, "v1 curve limit exceeded (2000 dates)");
        json curve = json::array();
        double peak = 0, maxdd = 0, prevnav = 0, prevflow = 0, index = 100;
        bool started = false, chain = true;
        for (auto &d : dates) {
            auto p = portfolio(data, d, false);
            json point = {{"date", d},
                          {"nav", p["nav"]},
                          {"pnl", p["economic_pnl"]},
                          {"return_index", nullptr}};
            if (p["complete"] == true) {
                double n = p["nav"], flow = p["net_external_flows"];
                if (!started && n > 0) {
                    started = true;
                    prevnav = n;
                    prevflow = flow;
                } else if (started && chain && prevnav > 0) {
                    double r = (n - (flow - prevflow)) / prevnav;
                    if (r < 0)
                        chain = false;
                    else
                        index *= r;
                    prevnav = n;
                    prevflow = flow;
                }
                if (started && chain) {
                    point["return_index"] = index;
                    peak = std::max(peak, index);
                    if (peak > 0)
                        maxdd = std::min(maxdd, index / peak - 1);
                }
            } else
                chain = false;
            curve.push_back(point);
        }
        result["curve"] = curve;
        result["max_drawdown"] = chain && started ? json(maxdd) : json(nullptr);
        result["return_method"] = "End-of-day external cashflow approximation, not intraday exact "
                                  "TWR; incomplete history breaks the chain";
    }
    return result;
}

inline json fundamentals(const json &data, const std::string &instrument, const std::string &asof) {
    date(asof);
    json selected = json::object(), history = json::array();
    for (auto &f : data["facts"])
        if (str(f, "instrument") == instrument && str(f, "filed") <= asof) {
            history.push_back(f);
            auto k = str(f, "concept") + "|" + str(f, "unit") + "|" + str(f, "period_kind");
            if (str(f, "period_kind") != "annual" && str(f, "period_kind") != "instant")
                continue;
            auto rank = [&](const json &x) {
                return std::make_tuple(str(x, "period_end"), str(x, "filed"),
                                       -x.value("priority", 100), str(x, "id"));
            };
            if (!selected.contains(k) || rank(f) > rank(selected[k]))
                selected[k] = f;
        }
    json facts = json::array();
    for (auto &[_, v] : selected.items())
        facts.push_back(v);
    return {{"instrument", instrument},
            {"as_of", asof},
            {"selected", facts},
            {"history", history},
            {"coverage", "Annual + instant normalized facts; quarterly history retained; no "
                         "synthesized TTM or consensus"}};
}
inline json dcf(const json &v) {
    auto get = [&](const char *k) {
        require(v.contains(k) && v[k].is_number(), std::string("Required number: ") + k);
        double n = v[k];
        require(std::isfinite(n), "Nonfinite input");
        return n;
    };
    double f = get("fcff"), growth = get("growth"), wacc = get("wacc"),
           terminal = get("terminal_growth"), debt = get("net_debt"), shares = get("shares");
    require(v.contains("years") && v["years"].is_number_integer(), "Integer years required");
    int years = v["years"];
    require(f > 0 && f <= 1e15 && shares > 0 && shares <= 1e15 && std::abs(debt) <= 1e15 &&
                years >= 1 && years <= 20 && growth > -.95 && growth <= 1 && wacc > 0 &&
                wacc <= 1 && terminal > -.5 && terminal < wacc,
            "Invalid DCF assumptions; require positive FCFF/shares and WACC > terminal growth");
    require(v.value("sector", "") != "Financials",
            "Industrial FCFF DCF not applicable to Financials");
    currency(str(v, "currency"));
    double pv = 0;
    json projections = json::array();
    for (int n = 1; n <= years; ++n) {
        f *= 1 + growth;
        double p = f / std::pow(1 + wacc, n);
        pv += p;
        projections.push_back({{"year", n}, {"fcff", f}, {"present_value", p}});
    }
    double tv = f * (1 + terminal) / (wacc - terminal) / std::pow(1 + wacc, years);
    double equity = pv + tv - debt;
    require(std::isfinite(equity / shares) && std::isfinite(tv) && std::isfinite(pv),
            "DCF numerical overflow");
    return {
        {"enterprise_value", pv + tv},
        {"equity_value", equity},
        {"per_share", equity / shares},
        {"terminal_share", tv / (pv + tv)},
        {"currency", v["currency"]},
        {"projections", projections},
        {"model",
         "User-supplied unlevered FCFF; operating cash flow minus capex is not automatically FCFF"},
        {"assumptions", v}};
}
inline json stress(const json &p, const json &input) {
    require(p["complete"] == true, "Cannot stress incomplete portfolio");
    double shock = input.value("equity_shock", -.1), fxshock = input.value("fx_shock", -.05);
    require(std::isfinite(shock) && std::isfinite(fxshock) && shock >= -1 && shock <= 1 &&
                fxshock >= -1 && fxshock <= 1,
            "Shock out of range");
    double loss = 0;
    json rows = json::array();
    for (auto &r : p["positions"]) {
        if (r["quantity"] == 0)
            continue;
        double mv = r["market_value_base"];
        double f = str(r, "currency") == "USD" ? 0 : fxshock;
        double delta = mv * ((1 + shock) * (1 + f) - 1);
        loss += delta;
        rows.push_back(
            {{"instrument", r["instrument"]}, {"strategy", r["strategy"]}, {"change", delta}});
    }
    for (auto &c : p["cash"])
        if (str(c, "currency") != "USD")
            loss += c["base_value"].get<double>() * fxshock;
    return {{"pnl", loss},
            {"nav_after", p["nav"].get<double>() + loss},
            {"positions", rows},
            {"equity_shock", shock},
            {"fx_shock", fxshock},
            {"margin_estimate", nullptr},
            {"model", p["scenario_model"]}};
}
} // namespace reach

#include "derivatives.hpp"
