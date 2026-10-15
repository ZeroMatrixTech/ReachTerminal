#pragma once
#include "imports.hpp"
namespace reach {
// Raw daily closes only: adjusted series must never price acquisition lots.
inline json alpha_daily_import(const json &raw, const json &ins) {
    require(str(ins, "country") == "US" && str(ins, "currency") == "USD",
            "Alpha daily v1 supports US/USD equities only");
    const auto symbol = str(ins, "symbol"), instrument = str(ins, "id");
    require(raw.contains("Meta Data") && raw.contains("Time Series (Daily)"),
            "Provider returned no daily data (key, quota or entitlement); "
            "nothing imported");
    require(str(raw.at("Meta Data"), "2. Symbol") == symbol, "Provider symbol mismatch");
    require(!raw.contains("Time Series (Daily Adjusted)"), "Adjusted prices not supported");
    const auto &series = raw.at("Time Series (Daily)");
    require(series.is_object() && !series.empty() && series.size() <= 100,
            "Expected compact daily series with 1-100 bars");
    auto url = "https://www.alphavantage.co/query?function=TIME_SERIES_DAILY&symbol=" + symbol;
    auto out = empty_data();
    std::string latest;
    for (auto it = series.begin(); it != series.end(); ++it) {
        const auto d = it.key();
        date(d);
        require(d <= today(), "Provider returned future daily bar");
        if (d == today())
            continue; // Exclude potentially unfinished daily sessions.
        auto text = [&](const char *k) { return flex_decimal(str(it.value(), k)); };
        json bar = {{"id", "av-bar-" + hash(instrument + ":" + d)},
                    {"instrument", instrument},
                    {"date", d},
                    {"currency", "USD"},
                    {"open", text("1. open")},
                    {"high", text("2. high")},
                    {"low", text("3. low")},
                    {"close", text("4. close")},
                    {"volume", text("5. volume")},
                    {"source", "Alpha Vantage"},
                    {"source_url", url},
                    {"adjustment", "raw"},
                    {"timezone", str(raw.at("Meta Data"), "5. Time Zone")}};
        validate("bars", bar);
        out["bars"].push_back(bar);
        out["quotes"].push_back({{"id", "av-close-" + hash(instrument + ":" + d)},
                                 {"instrument", instrument},
                                 {"date", d},
                                 {"currency", "USD"},
                                 {"price", bar["close"]},
                                 {"status", "eod"},
                                 {"source", "Alpha Vantage"},
                                 {"source_url", url}});
        latest = std::max(latest, d);
    }
    require(!latest.empty(), "No completed daily sessions returned");
    require(str(raw.at("Meta Data"), "3. Last Refreshed").substr(0, 10) >= latest,
            "Provider latest date inconsistent");
    out["raw_sources"].push_back({{"id", "av-raw-" + hash(instrument + raw.dump())},
                                  {"date", latest},
                                  {"source", "Alpha Vantage"},
                                  {"source_url", url},
                                  {"document", raw}});
    return out;
}
inline std::string fetch_alpha_daily(const std::string &symbol, const std::string &key) {
    require(std::regex_match(symbol, std::regex("[A-Z][A-Z0-9.]{0,14}")), "Invalid US symbol");
    require(std::regex_match(key, std::regex("[A-Za-z0-9]{1,100}")), "Invalid provider key");
    require(key != "demo" || symbol == "IBM",
            "Public demo key supports IBM only; configure your free API key for "
            "other symbols");
    const auto url =
        "https://www.alphavantage.co/query?function=TIME_SERIES_DAILY&symbol=" + symbol +
        "&apikey=" + key;
    auto c =
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
    require(bool(c), "curl init failed");
    std::string body;
    curl_easy_setopt(c.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(c.get(), CURLOPT_USERAGENT, "ReachTerminal/0.2");
    curl_easy_setopt(c.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c.get(), CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(c.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(
        c.get(), CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *u) -> size_t {
            auto &s = *static_cast<std::string *>(u);
            if (b && (a > (4 * 1024 * 1024 - s.size()) / b))
                return 0;
            s.append(p, a * b);
            return a * b;
        });
    curl_easy_setopt(c.get(), CURLOPT_WRITEDATA, &body);
    auto result = curl_easy_perform(c.get());
    long status = 0;
    curl_easy_getinfo(c.get(), CURLINFO_RESPONSE_CODE, &status);
    require(result == CURLE_OK && status == 200,
            "Daily provider unavailable (HTTP " + std::to_string(status) + "); no data imported");
    return body;
}
} // namespace reach
