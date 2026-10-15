#include "../backend/core.hpp"
#include "../backend/imports.hpp"
#include "../backend/market.hpp"
#include <functional>
#include <iostream>
using namespace reach;
int checks = 0;
void check(bool ok, const std::string &message) {
    ++checks;
    if (!ok)
        throw std::runtime_error("FAILED: " + message);
}
void near(double a, double b, const std::string &label) {
    check(std::abs(a - b) < 1e-5,
          label + " expected " + std::to_string(b) + " got " + std::to_string(a));
}
void rejects(std::function<void()> f, const std::string &label) {
    bool thrown = false;
    try {
        f();
    } catch (const std::exception &) {
        thrown = true;
    }
    check(thrown, label);
}
json instrument(std::string ccy = "USD") {
    return {{"id", "I"},          {"symbol", "TEST"},     {"name", "Test"},
            {"exchange", "TEST"}, {"country", "US"},      {"sector", "Industrials"},
            {"currency", ccy},    {"asset_class", "STK"}, {"sec_cik", "123"}};
}
json trade(std::string id, std::string date, std::string qty, std::string price,
           std::string fee = "0", std::string ccy = "USD", std::string strategy = "core") {
    return {{"id", id},
            {"account", "A"},
            {"instrument", "I"},
            {"strategy", strategy},
            {"date", date},
            {"quantity", qty},
            {"price", price},
            {"fee", fee},
            {"currency", ccy},
            {"source", "Test"},
            {"source_url", "manual:test"}};
}
json cash(std::string id, std::string date, std::string amount, std::string kind = "deposit",
          std::string ccy = "USD") {
    return {{"id", id},        {"account", "A"},   {"strategy", "core"},
            {"date", date},    {"amount", amount}, {"kind", kind},
            {"currency", ccy}, {"source", "Test"}, {"source_url", "manual:test"}};
}
json quote(std::string date, std::string price, std::string ccy = "USD") {
    return {{"id", "q-" + date}, {"instrument", "I"},
            {"date", date},      {"price", price},
            {"currency", ccy},   {"status", "manual"},
            {"source", "Test"},  {"source_url", "manual:test"}};
}
json rate(std::string date, std::string value) {
    return {{"id", "f-" + date}, {"date", date},     {"currency", "EUR"},          {"base", "USD"},
            {"rate", value},     {"source", "Test"}, {"source_url", "manual:test"}};
}
json setup(json batch, std::string ccy = "USD") {
    batch["instruments"] = json::array({instrument(ccy)});
    return merge_data(empty_data(), batch);
}
int main() {
    try {
        check(decimal("-0.123456") == -123456, "signed precision");
        check(decimal("1.000001") == 1000001, "six decimal exactness");
        rejects([] { decimal("0.0000001"); }, "reject overprecision");
        rejects([] { decimal("1e6"); }, "reject exponential accounting amounts");
        rejects([] { date("2024-02-30"); }, "reject invalid dates");
        auto d = setup({{"trades", json::array({trade("t1", "2024-01-02", "100", "150", "1"),
                                                trade("t2", "2024-02-01", "50", "180", "1"),
                                                trade("t3", "2024-03-01", "-120", "200", "2")})},
                        {"cashflows", json::array({cash("deposit", "2024-01-01", "20000"),
                                                   cash("div", "2024-02-15", "30", "dividend")})},
                        {"quotes", json::array({quote("2024-03-31", "210")})}});
        auto p = portfolio(d, "2024-03-31");
        auto row = p["positions"][0];
        near(row["quantity"], 30, "FIFO remaining qty");
        near(row["cost_local"], 5400.6, "buy fees retained in remaining basis");
        near(row["realized_base"], 5396.6,
             "FIFO realized incl fees (independent corrected oracle)");
        near(row["unrealized_base"], 899.4, "FIFO unrealized");
        near(p["cash_value"], 20026, "cash ledger");
        near(p["economic_pnl"], 6326, "economic P&L excludes deposit");
        check(merge_data(d, {{"trades", json::array({d["trades"][0]})}})["trades"].size() == 3,
              "idempotent import");
        auto collision = d["trades"][0];
        collision["price"] = "999";
        rejects([&] { merge_data(d, {{"trades", json::array({collision})}}); },
                "immutable ID conflict");
        auto e =
            setup({{"trades", json::array({trade("e1", "2024-01-10", "100", "120", "5", "EUR")})},
                   {"quotes", json::array({quote("2024-03-31", "130", "EUR")})},
                   {"fx", json::array({rate("2024-01-05", "1.1"), rate("2024-01-10", "1.1"),
                                       rate("2024-03-31", "1.2")})},
                   {"cashflows", json::array({cash("d", "2024-01-05", "20000")})}},
                  "EUR");
        auto leg1 = cash("fx1", "2024-01-10", "-13205.5", "fx_transfer");
        leg1["transfer_id"] = "X";
        auto leg2 = cash("fx2", "2024-01-10", "12005", "fx_transfer", "EUR");
        leg2["transfer_id"] = "X";
        e = merge_data(e, {{"cashflows", json::array({leg1, leg2})}});
        auto ep = portfolio(e, "2024-03-31");
        near(ep["nav"], 22394.5, "multi-currency NAV");
        near(ep["economic_pnl"], 2394.5, "historical vs current FX");
        near(ep["positions"][0]["price_pnl_base"], 1194, "price attribution at current FX");
        near(ep["positions"][0]["fx_pnl_base"], 1200.5, "FX attribution on historical basis");
        auto badrate = rate("2024-03-31", "1.3");
        badrate["id"] = "other-provider";
        rejects([&] { merge_data(e, {{"fx", json::array({badrate})}}); },
                "ambiguous dated FX rejected");
        auto missing = e;
        missing["fx"] = json::array({rate("2024-03-31", "1.2")});
        auto missingp = portfolio(missing, "2024-03-31");
        check(missingp["nav"].is_null() && missingp["realized_base"].is_null(),
              "missing historical FX suppresses aggregate (review allegation "
              "disproved)");
        rejects([&] { merge_data(empty_data(), {{"cashflows", json::array({leg1})}}); },
                "unpaired FX leg");
        auto scoped = setup(
            {{"trades", json::array({trade("s1", "2024-01-01", "100", "10", "0", "USD", "alpha"),
                                     trade("s2", "2024-01-02", "100", "20", "0", "USD", "beta"),
                                     trade("s3", "2024-01-03", "-100", "25", "0", "USD", "beta")})},
             {"quotes", json::array({quote("2024-01-03", "30")})}});
        auto sp = portfolio(scoped, "2024-01-03");
        for (auto &r : sp["positions"])
            if (r["strategy"] == "beta")
                near(r["realized_local"], 500, "explicit strategy matching");
        auto sell = trade("oversell", "2024-01-04", "-101", "30", "0", "USD", "alpha");
        auto oversold = merge_data(scoped, {{"trades", json::array({sell})}});
        rejects([&] { portfolio(oversold, "2024-01-04"); }, "no cross-strategy sell or short");
        auto split = setup({{"trades", json::array({trade("b", "2024-01-01", "10", "100"),
                                                    trade("s", "2024-01-03", "-5", "60")})},
                            {"splits", json::array({{{"id", "split"},
                                                     {"account", "A"},
                                                     {"instrument", "I"},
                                                     {"date", "2024-01-02"},
                                                     {"ratio", "2"},
                                                     {"source", "Test"},
                                                     {"source_url", "manual:test"}}})},
                            {"quotes", json::array({quote("2024-01-03", "60")})}});
        auto spl = portfolio(split, "2024-01-03");
        near(spl["positions"][0]["quantity"], 15, "split quantity");
        near(spl["positions"][0]["cost_local"], 750, "split leaves aggregate basis");
        near(spl["realized_base"], 50, "split sale realized");
        auto cashonly =
            setup({{"cashflows", json::array({cash("d", "2024-01-01", "10000"),
                                              cash("w", "2024-05-01", "-3000", "withdrawal"),
                                              cash("f", "2024-05-31", "-15", "fee"),
                                              cash("i", "2024-06-30", "4", "interest")})}});
        auto cp = portfolio(cashonly, "2024-06-30", true);
        near(cp["nav"], 6989, "withdrawal cash");
        near(cp["net_external_flows"], 7000, "net contributions");
        near(cp["economic_pnl"], -11, "fees/interest are returns");
        auto curve =
            setup({{"cashflows", json::array({cash("d", "2024-01-01", "100")})},
                   {"trades", json::array({trade("b", "2024-01-01", "1", "100")})},
                   {"quotes", json::array({quote("2024-01-01", "100"), quote("2024-01-02", "90"),
                                           quote("2024-01-03", "80")})}});
        auto cv = portfolio(curve, "2024-01-03", true);
        near(cv["curve"][2]["return_index"], 80,
             "loss days do not break curve (review allegation disproved)");
        near(cv["max_drawdown"], -.2, "flow-adjusted drawdown");
        auto cf = portfolio(cashonly, "2024-06-30", true);
        near(cf["curve"][1]["return_index"], 100, "withdrawal not a loss");
        json a = {
            {"fcff", 100.0},          {"growth", 0.0},  {"wacc", .1}, {"terminal_growth", 0.0},
            {"net_debt", 0.0},        {"shares", 10.0}, {"years", 5}, {"currency", "USD"},
            {"sector", "Industrials"}};
        near(dcf(a)["per_share"], 100, "perpetuity DCF independent closed form");
        auto invalid = a;
        invalid["terminal_growth"] = .1;
        rejects([&] { dcf(invalid); }, "terminal WACC constraint");
        invalid = a;
        invalid["sector"] = "Financials";
        rejects([&] { dcf(invalid); }, "bank DCF excluded");
        invalid = a;
        invalid["fcff"] = 1e308;
        rejects([&] { dcf(invalid); }, "DCF overflow input rejected");
        auto stressed = stress(ep, {{"equity_shock", -.1}, {"fx_shock", -.05}});
        near(stressed["pnl"], -2262, "joint price/FX stress");
        rejects([&] { stress(missingp, {}); }, "incomplete portfolio cannot stress");
        json raw = {{"cik", 123},
                    {"facts",
                     {{"us-gaap",
                       {{"Revenues",
                         {{"units",
                           {{"USD", json::array({{{"start", "2022-01-01"},
                                                  {"end", "2022-12-31"},
                                                  {"filed", "2023-02-15"},
                                                  {"val", 100},
                                                  {"accn", "0000000123-23-000001"},
                                                  {"form", "10-K"}},
                                                 {{"start", "2022-01-01"},
                                                  {"end", "2022-12-31"},
                                                  {"filed", "2023-06-01"},
                                                  {"val", 95},
                                                  {"accn", "0000000123-23-000002"},
                                                  {"form", "10-K/A"}},
                                                 {{"start", "2022-10-01"},
                                                  {"end", "2022-12-31"},
                                                  {"filed", "2023-02-15"},
                                                  {"val", 25},
                                                  {"accn", "0000000123-23-000001"},
                                                  {"form", "10-K"}}})}}}}}}}}}};
        auto sb = sec_import(raw, "I");
        auto fd = setup(sb);
        auto before = fundamentals(fd, "I", "2023-03-01"),
             after = fundamentals(fd, "I", "2023-07-01");
        near(number(dec(before["selected"][0], "value")), 100, "point-in-time original");
        near(number(dec(after["selected"][0], "value")), 95, "restatement after disclosure");
        check(before["history"].size() == 2, "annual and quarter history separate");
        check(fundamentals(fd, "I", "2023-01-01")["selected"].empty(),
              "no future financial leakage");
        auto wrong = sb;
        wrong["instruments"] = json::array({instrument()});
        wrong["instruments"][0]["sec_cik"] = "999";
        rejects([&] { merge_data(empty_data(), wrong); }, "SEC entity binding");
        auto malformed = fd["facts"][0];
        malformed["id"] = "malformed";
        malformed["period_start"] = "2022-10-01";
        rejects([&] { validate("facts", malformed); }, "annual vs quarter duration guard");
        std::string xml =
            R"(<FlexQueryResponse><FlexStatements><FlexStatement accountId="A" toDate="20240331"><Trades><Trade assetCategory="STK" levelOfDetail="EXECUTION" conid="123" symbol="TEST" currency="USD" listingExchange="XNAS" tradeID="1" tradeDate="20240101" dateTime="20240101;100000" quantity="10" buySell="BUY" tradePrice="100" ibCommission="-1" ibCommissionCurrency="USD"/></Trades><CashTransactions><CashTransaction transactionID="2" type="Deposits/Withdrawals" amount="2000" currency="USD" dateTime="20240101;090000"/></CashTransactions><OpenPositions><OpenPosition levelOfDetail="SUMMARY" assetCategory="STK" conid="123" currency="USD" position="10"/></OpenPositions><CashReport><CashReportCurrency currency="USD" endingCash="999"/></CashReport></FlexStatement></FlexStatements></FlexQueryResponse>)";
        auto flex = flex_import(xml);
        check(flex["trades"].size() == 1 && flex["cashflows"].size() == 1 &&
                  flex["reconciliations"].size() == 2,
              "Flex supported sections");
        near(number(dec(flex["trades"][0], "fee")), 1, "Flex negative commissions normalized");
        auto flexdata = merge_data(empty_data(), flex);
        auto fp = portfolio(flexdata, "2024-03-31");
        near(fp["cash"][0]["amount"], 999, "Flex cash ledger");
        check(flex_import(xml) == flex_import(xml), "Flex stable repeated conversion");
        rejects(
            [] {
                flex_import("<!DOCTYPE foo [<!ENTITY x SYSTEM "
                            "'file:///etc/passwd'>]><foo>&x;</foo>");
            },
            "XXE prohibited");
        rejects([] { flex_import("<FlexStatement><CorporateAction/></FlexStatement>"); },
                "unsupported corporate action fail-closed");
        check(decimal("5000000000000") == 5000000000000000000LL, "trillion scale fundamentals");
        rejects([] { decimal("9000000000000.000001"); }, "fixed point boundary overflow");
        check(flex_decimal("+10.00000000") == "10", "Flex trailing zero precision");
        auto stale = portfolio(e, "2024-04-09");
        check(stale["nav"].is_null(), "FX forward fill capped at seven days");
        auto negative_capex = fd["facts"][0];
        negative_capex["concept"] = "capex";
        negative_capex["value"] = "-1";
        rejects([&] { validate("facts", negative_capex); }, "canonical capex sign");
        auto duplicate_split = split["splits"][0];
        duplicate_split["id"] = "second-split";
        rejects([&] { merge_data(split, {{"splits", json::array({duplicate_split})}}); },
                "same date duplicate split rejected");
        {
            json av = {{"Meta Data",
                        {{"2. Symbol", "TEST"},
                         {"3. Last Refreshed", "2024-01-02"},
                         {"5. Time Zone", "US/Eastern"}}},
                       {"Time Series (Daily)",
                        {{"2024-01-02",
                          {{"1. open", "10.0000"},
                           {"2. high", "12.0000"},
                           {"3. low", "9.0000"},
                           {"4. close", "11.0000"},
                           {"5. volume", "100"}}}}}};
            auto daily = alpha_daily_import(av, instrument());
            check(daily["bars"].size() == 1 && daily["quotes"].size() == 1,
                  "raw daily maps bar and valuation quote");
            check(daily["quotes"][0]["price"] == "11" && daily["quotes"][0]["status"] == "eod",
                  "raw close and EOD label");
            auto md = merge_data(empty_data(), {{"instruments", json::array({instrument()})}});
            md = merge_data(md, daily);
            check(merge_data(md, daily) == md, "daily import idempotent");
            auto unfinished = av;
            unfinished["Time Series (Daily)"][today()] =
                unfinished["Time Series (Daily)"]["2024-01-02"];
            unfinished["Meta Data"]["3. Last Refreshed"] = today();
            check(alpha_daily_import(unfinished, instrument())["bars"].size() == 1,
                  "unfinished current day excluded");
            auto revised = av;
            revised["Time Series (Daily)"]["2024-01-02"]["4. close"] = "10.5";
            rejects([&] { merge_data(md, alpha_daily_import(revised, instrument())); },
                    "provider revision never silently overwrites");
            auto wrong = av;
            wrong["Meta Data"]["2. Symbol"] = "OTHER";
            rejects([&] { alpha_daily_import(wrong, instrument()); },
                    "wrong provider symbol rejected");
            rejects([&] { alpha_daily_import({{"Information", "quota"}}, instrument()); },
                    "quota message not treated as data");
            auto invalid = av;
            invalid["Time Series (Daily)"]["2024-01-02"]["2. high"] = "8";
            rejects([&] { alpha_daily_import(invalid, instrument()); },
                    "invalid provider candle rejected");
            auto adjusted = av;
            adjusted["Time Series (Daily Adjusted)"] = json::object();
            rejects([&] { alpha_daily_import(adjusted, instrument()); },
                    "adjusted series cannot value raw lots");
            rejects([&] { alpha_daily_import(av, instrument("EUR")); },
                    "currency mismatch rejected");
            auto future = av;
            future["Time Series (Daily)"]["2199-01-01"] =
                future["Time Series (Daily)"]["2024-01-02"];
            rejects([&] { alpha_daily_import(future, instrument()); },
                    "future market data rejected");
        }
        std::cout << json({{"ok", true}, {"checks", checks}}).dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
