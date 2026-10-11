#pragma once
#include "core.hpp"
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <memory>
#include <openssl/sha.h>

namespace reach {
inline std::string hash(const std::string &s) {
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), out);
    std::ostringstream h;
    for (auto c : out)
        h << std::hex << std::setw(2) << std::setfill('0') << int(c);
    return h.str();
}
inline std::string normalized_date(std::string s) {
    if (s.size() >= 10 && s[4] == '-')
        s = s.substr(0, 10);
    else if (s.size() >= 8)
        s = s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2);
    date(s);
    return s;
}
inline std::string decimal_text(const json &j) {
    require(j.is_number(), "SEC value must be numeric");
    long double n = j.get<long double>();
    require(std::isfinite(n) && std::abs(n) < 9e12L, "SEC value outside v1 numeric range");
    std::ostringstream s;
    s << std::fixed << std::setprecision(6) << n;
    return s.str();
}
inline std::string flex_decimal(std::string value) {
    if (!value.empty() && value[0] == '+')
        value.erase(0, 1);
    if (value.find('.') != std::string::npos) {
        while (!value.empty() && value.back() == '0')
            value.pop_back();
        if (!value.empty() && value.back() == '.')
            value.pop_back();
    }
    decimal(value);
    return value;
}
inline json sec_import(const json &raw, const std::string &instrument) {
    id(instrument);
    require(raw.contains("cik") && raw.contains("facts"), "Expected SEC CompanyFacts JSON");
    std::string cik = raw["cik"].is_string() ? raw["cik"].get<std::string>()
                                             : std::to_string(raw["cik"].get<uint64_t>());
    require(std::regex_match(cik, std::regex("[0-9]{1,10}")), "Invalid CIK");
    struct Mapping {
        std::string canonical;
        int priority;
    };
    const std::map<std::string, Mapping> mapping = {
        {"RevenueFromContractWithCustomerExcludingAssessedTax", {"revenue", 1}},
        {"Revenues", {"revenue", 2}},
        {"SalesRevenueNet", {"revenue", 3}},
        {"NetIncomeLoss", {"net_income", 1}},
        {"OperatingIncomeLoss", {"operating_income", 1}},
        {"Assets", {"assets", 1}},
        {"Liabilities", {"liabilities", 1}},
        {"StockholdersEquity", {"equity", 1}},
        {"CashAndCashEquivalentsAtCarryingValue", {"cash", 1}},
        {"NetCashProvidedByUsedInOperatingActivities", {"operating_cashflow", 1}},
        {"PaymentsToAcquirePropertyPlantAndEquipment", {"capex", 1}},
        {"EarningsPerShareDiluted", {"eps_diluted", 1}},
        {"LongTermDebtCurrent", {"debt_current", 1}},
        {"LongTermDebtNoncurrent", {"debt_noncurrent", 1}},
        {"CommonStockSharesOutstanding", {"shares", 2}},
        {"EntityCommonStockSharesOutstanding", {"shares", 1}}};
    json out = empty_data();
    int skipped = 0;
    std::set<std::string> ids;
    for (auto taxonomy : {"us-gaap", "dei"}) {
        if (!raw["facts"].contains(taxonomy))
            continue;
        for (auto &[tag, entry] : raw["facts"][taxonomy].items()) {
            if (!mapping.contains(tag) || !entry.contains("units"))
                continue;
            for (auto &[unit, values] : entry["units"].items())
                for (auto &f : values) {
                    if (!f.contains("val") || !f.contains("end") || !f.contains("filed") ||
                        !f.contains("accn")) {
                        ++skipped;
                        continue;
                    }
                    auto m = mapping.at(tag);
                    bool instant = m.canonical == "assets" || m.canonical == "liabilities" ||
                                   m.canonical == "equity" || m.canonical == "cash" ||
                                   m.canonical == "shares" || m.canonical.starts_with("debt_");
                    bool monetary = std::regex_match(unit, std::regex("[A-Z]{3}"));
                    bool unitsok = m.canonical == "shares" ? unit == "shares"
                                   : m.canonical == "eps_diluted"
                                       ? std::regex_match(unit, std::regex("[A-Z]{3}/shares"))
                                       : monetary;
                    if (!unitsok || (instant && f.contains("start")) ||
                        (!instant && !f.contains("start"))) {
                        ++skipped;
                        continue;
                    }
                    auto end = str(f, "end"), filed = str(f, "filed"), accn = str(f, "accn"),
                         start = f.value("start", "");
                    int duration = instant ? 0 : days(start, end);
                    std::string kind = instant                              ? "instant"
                                       : duration >= 330 && duration <= 400 ? "annual"
                                       : duration >= 60 && duration <= 120  ? "quarter"
                                                                            : "other";
                    std::string acc = accn;
                    acc.erase(std::remove(acc.begin(), acc.end(), '-'), acc.end());
                    json fact = {{"instrument", instrument},
                                 {"entity_cik", cik},
                                 {"concept", m.canonical},
                                 {"unit", unit},
                                 {"value", decimal_text(f["val"])},
                                 {"period_start", start},
                                 {"period_end", end},
                                 {"period_kind", kind},
                                 {"filed", filed},
                                 {"accession", accn},
                                 {"taxonomy", taxonomy},
                                 {"tag", tag},
                                 {"priority", m.priority},
                                 {"source", "SEC CompanyFacts"},
                                 {"source_url", "https://www.sec.gov/Archives/edgar/data/" +
                                                    std::to_string(std::stoull(cik)) + "/" + acc +
                                                    "/"},
                                 {"form", f.value("form", "")}};
                    auto key = instrument + "|" + taxonomy + "|" + tag + "|" + unit + "|" + start +
                               "|" + end + "|" + filed + "|" + accn + "|" +
                               fact["value"].get<std::string>();
                    fact["id"] = "sec-" + hash(key);
                    if (ids.insert(str(fact, "id")).second)
                        out["facts"].push_back(fact);
                }
        }
    }
    require(!out["facts"].empty(),
            "No supported US-GAAP/DEI facts; IFRS/custom tags need reviewed manual mapping");
    std::string latest = "1900-01-01";
    for (auto &f : out["facts"])
        latest = std::max(latest, str(f, "filed"));
    out["raw_sources"].push_back(
        {{"id", "sec-raw-" + hash(raw.dump())},
         {"source", "SEC CompanyFacts"},
         {"source_url", "https://data.sec.gov/api/xbrl/companyfacts/CIK" +
                            std::string(10 - cik.size(), '0') + cik + ".json"},
         {"date", latest},
         {"entity_cik", cik},
         {"document", raw}});
    return out;
}
inline json flex_import(const std::string &xml) {
    require(xml.size() <= 32 * 1024 * 1024, "Flex file too large");
    require(xml.find("<!DOCTYPE") == std::string::npos && xml.find("<!ENTITY") == std::string::npos,
            "DTD/entities prohibited");
    auto doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>(
        xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "flex.xml", nullptr,
                      XML_PARSE_NONET | XML_PARSE_NOBLANKS),
        xmlFreeDoc);
    require(bool(doc), "Invalid Flex XML");
    require(!doc->intSubset && !doc->extSubset, "DTD prohibited");
    json out = empty_data();
    std::set<std::string> instruments;
    auto prop = [](xmlNode *n, const char *name) {
        auto p = xmlGetProp(n, BAD_CAST name);
        std::string s = p ? reinterpret_cast<const char *>(p) : "";
        if (p)
            xmlFree(p);
        return s;
    };
    std::function<void(xmlNode *, std::string, std::string)> walk = [&](xmlNode *n,
                                                                        std::string account,
                                                                        std::string end) {
        for (; n; n = n->next) {
            if (n->type != XML_ELEMENT_NODE)
                continue;
            auto scopeAccount = account, scopeEnd = end;
            std::string name = reinterpret_cast<const char *>(n->name);
            auto a = prop(n, "accountId");
            if (!a.empty())
                account = a;
            if (name == "FlexStatement") {
                auto v = prop(n, "toDate");
                if (!v.empty())
                    end = normalized_date(v);
            }
            if (name == "CorporateAction")
                throw std::runtime_error("Flex corporate actions require reviewed normalized "
                                         "split/adjustment import; refusing partial accounting");
            if (name == "Trade") {
                require(prop(n, "assetCategory") == "STK",
                        "Flex v1 accepts STK only; import other assets unsupported");
                auto level = prop(n, "levelOfDetail");
                require(level.empty() || level == "EXECUTION",
                        "Flex Trades must use EXECUTION detail only");
                auto conid = prop(n, "conid"), ccy = prop(n, "currency"),
                     symbol = prop(n, "symbol"), exchange = prop(n, "listingExchange");
                require(!conid.empty() && !account.empty(), "Flex requires conid and accountId");
                auto instrument = "ibkr:" + conid;
                if (instruments.insert(instrument).second)
                    out["instruments"].push_back(
                        {{"id", instrument},
                         {"symbol", symbol},
                         {"name", prop(n, "description").empty() ? symbol : prop(n, "description")},
                         {"currency", ccy},
                         {"exchange", exchange.empty() ? "UNKNOWN" : exchange},
                         {"country", "Unknown"},
                         {"sector", "Unknown"},
                         {"asset_class", "STK"}});
                auto commissionCurrency = prop(n, "ibCommissionCurrency");
                require(commissionCurrency.empty() || commissionCurrency == ccy,
                        "Commission currency differs; normalize cash fees separately");
                auto fee = prop(n, "ibCommission");
                if (fee.empty())
                    fee = "0";
                if (fee[0] == '-')
                    fee = fee.substr(1);
                auto tid = prop(n, "tradeID");
                if (tid.empty())
                    tid = prop(n, "transactionID");
                require(!tid.empty(), "Flex requires tradeID or transactionID");
                auto d = prop(n, "tradeDate");
                if (d.empty())
                    d = prop(n, "dateTime");
                auto strategy = prop(n, "orderReference");
                if (strategy.empty())
                    strategy = "unallocated";
                json trade = {{"id", "ibkr:" + account + ":" + tid},
                              {"account", account},
                              {"instrument", instrument},
                              {"strategy", strategy},
                              {"date", normalized_date(d)},
                              {"quantity", flex_decimal(prop(n, "quantity"))},
                              {"price", flex_decimal(prop(n, "tradePrice"))},
                              {"fee", flex_decimal(fee)},
                              {"currency", ccy},
                              {"source", "IBKR Flex"},
                              {"source_url", "manual:ibkr-flex"}};
                auto seq = prop(n, "dateTime");
                if (seq.size() >= 15) {
                    std::string tm = seq.substr(9);
                    tm.erase(std::remove(tm.begin(), tm.end(), ':'), tm.end());
                    if (std::regex_match(tm, std::regex("[0-9]{6}")))
                        trade["sequence"] = std::stoi(tm);
                }
                require(prop(n, "buySell") == "BUY" || prop(n, "buySell") == "SELL",
                        "Invalid buySell");
                require((prop(n, "buySell") == "BUY") ==
                            (decimal(flex_decimal(prop(n, "quantity"))) > 0),
                        "Flex quantity/buySell mismatch");
                out["trades"].push_back(trade);
            }
            if (name == "CashTransaction") {
                auto t = prop(n, "type"), amount = flex_decimal(prop(n, "amount")),
                     tid = prop(n, "transactionID"), ccy = prop(n, "currency");
                require(!tid.empty(), "CashTransaction requires transactionID");
                std::string kind;
                if (t == "Deposits/Withdrawals")
                    kind = decimal(amount) > 0 ? "deposit" : "withdrawal";
                else if (t == "Dividends" || t == "Payment In Lieu Of Dividends")
                    kind = "dividend";
                else if (t == "Withholding Tax")
                    kind = "tax";
                else if (t == "Broker Interest Received" || t == "Broker Interest Paid")
                    kind = "interest";
                else if (t == "Other Fees")
                    kind = "fee";
                else
                    throw std::runtime_error("Unsupported Flex cash type: " + t +
                                             "; normalize explicitly rather than omit");
                auto d = prop(n, "dateTime");
                if (d.empty())
                    d = prop(n, "reportDate");
                out["cashflows"].push_back({{"id", "ibkr-cash:" + account + ":" + tid},
                                            {"account", account},
                                            {"strategy", "unallocated"},
                                            {"date", normalized_date(d)},
                                            {"currency", ccy},
                                            {"amount", amount},
                                            {"kind", kind},
                                            {"source", "IBKR Flex"},
                                            {"source_url", "manual:ibkr-flex"}});
            }
            if (name == "OpenPosition" && prop(n, "levelOfDetail") == "SUMMARY") {
                require(prop(n, "assetCategory") == "STK", "Unsupported open-position asset");
                auto d = prop(n, "reportDate");
                if (d.empty())
                    d = end;
                d = normalized_date(d);
                auto conid = prop(n, "conid");
                out["reconciliations"].push_back(
                    {{"id", "ibkr-position:" + account + ":" + conid + ":" + d},
                     {"account", account},
                     {"instrument", "ibkr:" + conid},
                     {"date", d},
                     {"currency", prop(n, "currency")},
                     {"quantity", flex_decimal(prop(n, "position"))},
                     {"source", "IBKR Flex"},
                     {"source_url", "manual:ibkr-flex"}});
            }
            if (name == "CashReportCurrency" && prop(n, "currency") != "BASE_SUMMARY") {
                require(!end.empty(), "Cash report requires statement toDate");
                auto c = prop(n, "currency");
                auto cash = prop(n, "endingCash");
                if (!cash.empty())
                    out["reconciliations"].push_back(
                        {{"id", "ibkr-cashcheck:" + account + ":" + c + ":" + end},
                         {"account", account},
                         {"date", end},
                         {"currency", c},
                         {"cash", flex_decimal(cash)},
                         {"source", "IBKR Flex"},
                         {"source_url", "manual:ibkr-flex"}});
            }
            walk(n->children, account, end);
            account = scopeAccount;
            end = scopeEnd;
        }
    };
    walk(xmlDocGetRootElement(doc.get()), "", "");
    require(!out["trades"].empty() || !out["cashflows"].empty(),
            "Flex contains no supported ledger events");
    std::string latest = "1900-01-01";
    for (auto kind : {"trades", "cashflows", "reconciliations"})
        for (auto &r : out[kind])
            latest = std::max(latest, str(r, "date"));
    out["raw_sources"].push_back({{"id", "flex-raw-" + hash(xml)},
                                  {"date", latest},
                                  {"source", "IBKR Flex"},
                                  {"source_url", "manual:ibkr-flex"},
                                  {"document", xml}});
    return out;
}
inline std::string fetch_sec(std::string cik, const std::string &ua) {
    require(std::regex_match(cik, std::regex("[0-9]{1,10}")), "CIK must have 1-10 digits");
    require(ua.size() >= 10 && ua.find('@') != std::string::npos,
            "Set REACH_SEC_USER_AGENT to project name and contact email");
    cik = std::string(10 - cik.size(), '0') + cik;
    auto url = "https://data.sec.gov/api/xbrl/companyfacts/CIK" + cik + ".json";
    auto c =
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
    require(bool(c), "curl init failed");
    std::string body;
    curl_easy_setopt(c.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(c.get(), CURLOPT_USERAGENT, ua.c_str());
    curl_easy_setopt(c.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c.get(), CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(c.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(
        c.get(), CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *u) -> size_t {
            auto &s = *static_cast<std::string *>(u);
            if (s.size() + a * b > 32 * 1024 * 1024)
                return 0;
            s.append(p, a * b);
            return a * b;
        });
    curl_easy_setopt(c.get(), CURLOPT_WRITEDATA, &body);
    auto result = curl_easy_perform(c.get());
    long status = 0;
    curl_easy_getinfo(c.get(), CURLINFO_RESPONSE_CODE, &status);
    require(result == CURLE_OK && status == 200,
            "SEC unavailable; import downloaded CompanyFacts JSON instead (HTTP " +
                std::to_string(status) + ")");
    return body;
}
} // namespace reach
