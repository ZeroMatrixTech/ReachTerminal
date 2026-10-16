#pragma once
#include "imports.hpp"
#include <ctime>
namespace reach {
inline int64_t epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline std::string epoch_date(int64_t ms) {
    std::time_t t = ms / 1000;
    std::tm tm{};
    require(gmtime_r(&t, &tm) != nullptr, "Invalid epoch");
    std::ostringstream s;
    s << std::put_time(&tm, "%Y-%m-%d");
    date(s.str());
    return s.str();
}
inline const std::map<std::string, std::string> macro_indicators = {
    {"gdp", "NY.GDP.MKTP.CD"}, {"growth", "NY.GDP.MKTP.KD.ZG"}, {"inflation", "FP.CPI.TOTL.ZG"}};
inline std::string worldbank_url(const std::string &country, const std::string &indicator) {
    require(
        std::set<std::string>{"USA", "CHN", "DEU", "JPN", "GBR", "HKG", "FRA", "IND", "BRA", "AUS"}
            .contains(country),
        "Unsupported macro country");
    require(macro_indicators.contains(indicator), "Unsupported macro indicator");
    return "https://api.worldbank.org/v2/country/" + country + "/indicator/" +
           macro_indicators.at(indicator) + "?format=json&date=2010:2025&per_page=100";
}
inline std::string public_fetch(const std::string &mode, const std::string &url) {
    require(mode == "user", "External public data fetch disabled outside user workspace");
    require(url == "https://www.federalreserve.gov/feeds/press_all.xml" ||
                std::regex_match(
                    url, std::regex("https://api\\.worldbank\\.org/v2/country/"
                                    "(USA|CHN|DEU|JPN|GBR|HKG|FRA|IND|BRA|AUS)/indicator/"
                                    "(NY\\.GDP\\.MKTP\\.CD|NY\\.GDP\\.MKTP\\.KD\\.ZG|FP\\.CPI\\."
                                    "TOTL\\.ZG)\\?format=json&date=2010:2025&per_page=100")),
            "Public endpoint prohibited");
    auto c =
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
    require(bool(c), "curl init");
    std::string body;
    curl_easy_setopt(c.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(c.get(), CURLOPT_PROXY, "");
    curl_easy_setopt(c.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c.get(), CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c.get(), CURLOPT_USERAGENT, "ReachTerminal/0.1.0 public research");
    curl_easy_setopt(
        c.get(), CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *u) -> size_t {
            auto &s = *static_cast<std::string *>(u);
            if (b && a > (4 * 1024 * 1024 - s.size()) / b)
                return 0;
            s.append(p, a * b);
            return a * b;
        });
    curl_easy_setopt(c.get(), CURLOPT_WRITEDATA, &body);
    auto code = curl_easy_perform(c.get());
    long status = 0;
    curl_easy_getinfo(c.get(), CURLINFO_RESPONSE_CODE, &status);
    require(code == CURLE_OK && status == 200, "Official public source unavailable (HTTP " +
                                                   std::to_string(status) +
                                                   "); no substitute data generated");
    return body;
}
inline json worldbank_import(const json &raw, const std::string &country,
                             const std::string &indicator, int64_t retrieved) {
    auto url = worldbank_url(country, indicator);
    require(raw.is_array() && raw.size() == 2 && raw[0].is_object() && raw[1].is_array() &&
                raw[1].size() <= 100,
            "Invalid World Bank response");
    require(raw[0].value("pages", 0) == 1, "Paginated World Bank response not fully loaded");
    json out = empty_data();
    auto seen = epoch_date(retrieved);
    date(seen);
    require(seen <= today(), "Future retrieval date");
    for (auto &r : raw[1]) {
        require(str(r, "countryiso3code") == country &&
                    str(r.at("indicator"), "id") == macro_indicators.at(indicator),
                "World Bank country/indicator mismatch");
        auto year = str(r, "date");
        require(std::regex_match(year, std::regex("20[0-9]{2}")), "Invalid macro reporting year");
        require(year + "-12-31" <= seen, "Future macro reporting period");
        require(r.contains("value") && (r["value"].is_null() || r["value"].is_number()),
                "Invalid macro value");
        if (!r["value"].is_null())
            require(std::isfinite(r["value"].get<double>()) &&
                        std::abs(r["value"].get<double>()) <= 1e18,
                    "Macro value outside range");
        json o = {{"country", country},
                  {"indicator", indicator},
                  {"period_end", year + "-12-31"},
                  {"frequency", "annual"},
                  {"value", r["value"]},
                  {"unit", indicator == "gdp" ? "current USD" : "percent"},
                  {"known_at", nullptr},
                  {"retrieved_at_ms", retrieved},
                  {"date", seen},
                  {"source", "World Bank WDI"},
                  {"source_url", url}};
        o["id"] = "macro-" + hash(o.dump());
        out["macro_observations"].push_back(o);
    }
    out["raw_sources"].push_back({{"id", "wb-raw-" + hash(raw.dump() + std::to_string(retrieved))},
                                  {"date", seen},
                                  {"source", "World Bank WDI"},
                                  {"source_url", url},
                                  {"document", raw.is_array() ? json{{"response", raw}} : raw}});
    return out;
}
inline json macro_view(const json &data, const std::string &asof) {
    date(asof);
    json selected = json::object(), history = json::array();
    for (auto &o : data["macro_observations"])
        if (str(o, "date") <= asof) {
            history.push_back(o);
            auto key = str(o, "country") + "/" + str(o, "indicator") + "/" + str(o, "period_end");
            if (!selected.contains(key) ||
                std::make_pair(o["retrieved_at_ms"].get<int64_t>(), str(o, "id")) >
                    std::make_pair(selected[key]["retrieved_at_ms"].get<int64_t>(),
                                   str(selected[key], "id")))
                selected[key] = o;
        }
    json rows = json::array();
    for (auto &[k, o] : selected.items())
        rows.push_back(o);
    return {{"as_of", asof},
            {"observations", rows},
            {"history", history},
            {"basis",
             "Annual WDI observation periods are not publication dates. Historical release "
             "dates unknown; as-of visibility starts at actual retrieval, never backfilled."}};
}
inline json fed_news_import(const std::string &xml) {
    require(xml.size() <= 4 * 1024 * 1024, "Feed too large");
    auto doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>(
        xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "fed.xml", nullptr,
                      XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING),
        xmlFreeDoc);
    require(doc && !doc->intSubset && !doc->extSubset, "Invalid RSS or prohibited DTD");
    auto root = xmlDocGetRootElement(doc.get());
    require(root && std::string(reinterpret_cast<const char *>(root->name)) == "rss",
            "RSS required");
    auto text = [](xmlNode *n, const char *name) {
        for (auto *c = n->children; c; c = c->next)
            if (c->type == XML_ELEMENT_NODE && xmlStrEqual(c->name, BAD_CAST name)) {
                auto p = xmlNodeGetContent(c);
                std::string s = p ? reinterpret_cast<char *>(p) : "";
                xmlFree(p);
                return s;
            }
        return std::string();
    };
    json out = empty_data();
    int count = 0;
    std::string latest = "1900-01-01";
    for (auto *channel = root->children; channel; channel = channel->next)
        if (xmlStrEqual(channel->name, BAD_CAST "channel"))
            for (auto *n = channel->children; n; n = n->next)
                if (xmlStrEqual(n->name, BAD_CAST "item")) {
                    require(++count <= 200, "Feed item limit");
                    auto title = text(n, "title"), url = text(n, "link"),
                         published = text(n, "pubDate"), summary = text(n, "description");
                    require(url.starts_with("https://www.federalreserve.gov/") &&
                                title.size() > 0 && title.size() <= 2000,
                            "Invalid official news link/title");
                    require(std::regex_search(published, std::regex("(GMT|[+-][0-9]{4})$")),
                            "RSS date needs explicit timezone");
                    auto epoch = curl_getdate(published.c_str(), nullptr);
                    require(epoch >= 0 && static_cast<int64_t>(epoch) * 1000 <= epoch_ms() + 60000,
                            "Invalid or future RSS publication date");
                    auto d = epoch_date(static_cast<int64_t>(epoch) * 1000);
                    require(d <= today(), "Future RSS publication");
                    latest = std::max(latest, d);
                    json news = {{"title", title},
                                 {"date", d},
                                 {"published_raw", published},
                                 {"source", "Federal Reserve"},
                                 {"source_url", url},
                                 {"summary", summary.substr(0, 2000)}};
                    news["id"] = "fed-" + hash(news.dump());
                    out["news"].push_back(news);
                }
    require(count > 0, "Feed contains no items");
    out["raw_sources"].push_back(
        {{"id", "fed-raw-" + hash(xml)},
         {"date", latest},
         {"source", "Federal Reserve"},
         {"source_url", "https://www.federalreserve.gov/feeds/press_all.xml"},
         {"document", xml}});
    return out;
}
} // namespace reach
