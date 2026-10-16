#pragma once
#include "core.hpp"
namespace reach {
inline double model_number(const json &v, const char *k, double low, double high) {
    require(v.contains(k) && v[k].is_number(), std::string("Required model number: ") + k);
    double n = v[k];
    require(std::isfinite(n) && n >= low && n <= high, std::string("Model domain: ") + k);
    return n;
}
inline double normal_cdf(double x) {
    return .5 * std::erfc(-x / std::sqrt(2.));
}
inline json european_option(const json &v) {
    double s = model_number(v, "spot", .000001, 1e9), k = model_number(v, "strike", .000001, 1e9),
           t = model_number(v, "years", 0, 20), r = model_number(v, "rate", -.5, 1),
           q = model_number(v, "yield", -.5, 1), vol = model_number(v, "volatility", .0001, 5);
    auto right = str(v, "right");
    require(right == "call" || right == "put", "European call/put only");
    require(v.value("style", std::string("European")) == "European",
            "American/exotic option pricing not supported");
    if (t == 0)
        return {{"price", std::max(0., right == "call" ? s - k : k - s)},
                {"delta", nullptr},
                {"gamma", nullptr},
                {"vega_1pp", nullptr},
                {"theta_day", nullptr},
                {"rho_1pp", nullptr},
                {"expired", true}};
    double rt = std::sqrt(t), d1 = (std::log(s / k) + (r - q + vol * vol / 2) * t) / (vol * rt),
           d2 = d1 - vol * rt, dq = std::exp(-q * t), dr = std::exp(-r * t),
           pdf = std::exp(-d1 * d1 / 2) / std::sqrt(2 * std::acos(-1.)),
           common = -s * dq * pdf * vol / (2 * rt);
    bool call = right == "call";
    double price = call ? s * dq * normal_cdf(d1) - k * dr * normal_cdf(d2)
                        : k * dr * normal_cdf(-d2) - s * dq * normal_cdf(-d1);
    double theta = call ? common - r * k * dr * normal_cdf(d2) + q * s * dq * normal_cdf(d1)
                        : common + r * k * dr * normal_cdf(-d2) - q * s * dq * normal_cdf(-d1);
    return {{"price", std::max(0., price)},
            {"delta", call ? dq * normal_cdf(d1) : dq * (normal_cdf(d1) - 1)},
            {"gamma", dq * pdf / (s * vol * rt)},
            {"vega_1pp", s * dq * pdf * rt * .01},
            {"theta_day", theta / 365.},
            {"rho_1pp", (call ? k * t * dr * normal_cdf(d2) : -k * t * dr * normal_cdf(-d2)) * .01},
            {"expired", false}};
}
inline json hedge_book(const json &v) {
    currency(str(v, "currency"));
    require(v.contains("positions") && v["positions"].is_array() && !v["positions"].empty() &&
                v["positions"].size() <= 100,
            "Hedge book needs 1-100 modeled positions");
    json rows = json::array(), groups = json::object();
    double value = 0, vega = 0, theta = 0;
    std::set<std::string> ids;
    for (auto &p : v["positions"]) {
        id(str(p, "id"));
        require(ids.insert(str(p, "id")).second, "Duplicate hedge position ID");
        auto type = str(p, "type"), u = str(p, "underlying");
        id(u);
        require(!p.contains("currency") || p["currency"] == v["currency"],
                "Cross-currency hedge book prohibited");
        double s = model_number(p, "spot", .000001, 1e9),
               qty = model_number(p, "quantity", -1e7, 1e7),
               mult = model_number(p, "multiplier", .000001, 1e6);
        require(qty != 0, "Zero hedge quantity");
        require(type == "STK" || type == "OPT" || type == "FUT", "Unsupported hedge asset");
        require(type != "STK" || mult == 1, "Stock multiplier must be one");
        json m = type == "OPT" ? european_option(p)
                               : json{{"price", s},     {"delta", 1.},     {"gamma", 0.},
                                      {"vega_1pp", 0.}, {"theta_day", 0.}, {"expired", false}};
        require(!m["expired"].get<bool>(), "Initial options must have positive time to expiry");
        double n = qty * mult, d = m["delta"].get<double>() * n, g = m["gamma"].get<double>() * n;
        // Futures have zero funded market value at the scenario origin; P&L is price change.
        double mv = type == "FUT" ? 0 : m["price"].get<double>() * n;
        value += mv;
        vega += m["vega_1pp"].get<double>() * n;
        theta += m["theta_day"].get<double>() * n;
        if (!groups.contains(u))
            groups[u] = {{"delta_units", 0.}, {"delta_notional", 0.}, {"gamma_half_notional", 0.}};
        groups[u]["delta_units"] = groups[u]["delta_units"].get<double>() + d;
        groups[u]["delta_notional"] = groups[u]["delta_notional"].get<double>() + d * s;
        groups[u]["gamma_half_notional"] =
            groups[u]["gamma_half_notional"].get<double>() + .5 * g * s * s;
        rows.push_back({{"id", p["id"]},
                        {"type", type},
                        {"underlying", u},
                        {"unit_model", m},
                        {"market_value", mv},
                        {"delta_units", d},
                        {"gamma_units", g},
                        {"vega_1pp", m["vega_1pp"].get<double>() * n}});
    }
    auto shocks = v.value("scenario", json{{"spot", -.1}, {"volatility", .05}, {"days", 7}});
    double ds = model_number(shocks, "spot", -.99, 2),
           dv = model_number(shocks, "volatility", -4.9, 4.9),
           dd = model_number(shocks, "days", 0, 3650);
    double pnl = 0;
    json scenario_rows = json::array();
    for (size_t i = 0; i < v["positions"].size(); ++i) {
        auto p = v["positions"][i];
        double n = p["quantity"].get<double>() * p["multiplier"].get<double>(),
               s = p["spot"].get<double>();
        double change = s * ds;
        if (p["type"] == "OPT") {
            p["spot"] = s * (1 + ds);
            p["years"] = std::max(0., p["years"].get<double>() - dd / 365.);
            p["volatility"] = p["volatility"].get<double>() + dv;
            require(p["volatility"].get<double>() >= .0001 && p["volatility"].get<double>() <= 5,
                    "Scenario volatility outside model domain");
            auto repriced = european_option(p);
            change = repriced["price"].get<double>() - rows[i]["unit_model"]["price"].get<double>();
        }
        pnl += change * n;
        scenario_rows.push_back({{"id", p["id"]}, {"pnl", change * n}});
    }
    for (auto &[u, g] : groups.items())
        g["delta_neutral_stock_units"] = -g["delta_units"].get<double>();
    require(std::isfinite(value) && std::isfinite(pnl) && std::isfinite(vega) &&
                std::isfinite(theta),
            "Hedge model overflow");
    return {{"currency", v["currency"]},
            {"market_value", value},
            {"vega_1pp", vega},
            {"theta_day", theta},
            {"underlyings", groups},
            {"positions", rows},
            {"scenario_pnl", pnl},
            {"scenario_rows", scenario_rows},
            {"scenario", shocks},
            {"basis", "Modeled single-currency hedge book; European Black-Scholes continuous "
                      "yield; futures are linear contract-price shocks with no basis/carry model. "
                      "Not a trade ledger, margin rule, or executable recommendation."}};
}
} // namespace reach
