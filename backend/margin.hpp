#pragma once
#include "public_data.hpp"
namespace reach {
inline json broker_margin(const json &raw, const std::string &account, int64_t fetched) {
    require(raw.is_object(), "Invalid broker summary");
    json rows = json::array();
    std::map<std::string, json> values;
    const std::map<std::string, std::string> keys = {
        {"netliquidation", "净清算值"},        {"initmarginreq", "初始保证金"},
        {"maintmarginreq", "维持保证金"},      {"excessliquidity", "剩余流动性"},
        {"availablefunds", "可用资金"},        {"buyingpower", "购买力"},
        {"equitywithloanvalue", "含借贷权益"}, {"lookaheadexcessliquidity", "前瞻剩余流动性"}};
    for (auto &[key, label] : keys) {
        json row = {{"key", key},
                    {"label", label},
                    {"value", nullptr},
                    {"currency", nullptr},
                    {"broker_timestamp", nullptr},
                    {"stale", true}};
        if (raw.contains(key) && raw[key].is_object()) {
            auto &r = raw[key];
            if (r.contains("isNull") && r["isNull"] == false && r.contains("amount") &&
                r["amount"].is_number() && std::isfinite(r["amount"].get<double>()))
                row["value"] = r["amount"];
            if (r.contains("currency") && r["currency"].is_string() &&
                std::regex_match(r["currency"].get<std::string>(), std::regex("[A-Z]{3}")))
                row["currency"] = r["currency"];
            if (r.contains("timestamp") && r["timestamp"].is_number_integer()) {
                auto t = r["timestamp"].get<int64_t>();
                row["broker_timestamp"] = t;
                row["stale"] = t <= 0 || t > fetched + 60000 || fetched - t > 120000;
            }
        }
        rows.push_back(row);
        values[key] = row;
    }
    std::string status = "unknown";
    json ratio = nullptr;
    auto &e = values["excessliquidity"];
    auto &n = values["netliquidation"];
    if (!e["value"].is_null() && !e["currency"].is_null() && !e["stale"].get<bool>()) {
        status = e["value"].get<double>() <= 0 ? "critical" : "reported";
        if (!n["value"].is_null() && n["value"].get<double>() > 0 && !n["stale"].get<bool>() &&
            n["currency"] == e["currency"] &&
            std::abs(n["broker_timestamp"].get<int64_t>() - e["broker_timestamp"].get<int64_t>()) <=
                60000) {
            ratio = e["value"].get<double>() / n["value"].get<double>();
            if (status != "critical" && ratio.get<double>() < .1)
                status = "low-buffer";
        }
    }
    return {{"account", account},
            {"source", "IBKR broker-reported account totals"},
            {"retrieved_at_ms", fetched},
            {"fields", rows},
            {"liquidity_ratio", ratio},
            {"status", status},
            {"basis",
             "Broker reported totals only, no segment sums. Ratio is excess liquidity / net "
             "liquidation when currencies and timestamps align; 10% is a local warning "
             "threshold, not a liquidation rule. Missing or old data stays unknown."}};
}
} // namespace reach
