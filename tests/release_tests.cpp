#include "../backend/derivatives.hpp"
#include "../backend/margin.hpp"
#include <iostream>
#include <fstream>
using namespace reach;
int main(int argc,char**argv) {
    int count = 0;
    auto check = [&](bool b, const char *m) {
        ++count;
        if (!b)
            throw std::runtime_error(m);
    };
    auto near = [&](double a, double b, double tol, const char *m) {
        check(std::abs(a - b) < tol, m);
    };
    auto rejects = [&](auto f, const char *m) {
        bool bad = false;
        try {
            f();
        } catch (const std::exception &) {
            bad = true;
        }
        check(bad, m);
    };
    json o = {{"spot", 100.}, {"strike", 100.},   {"years", 1.},    {"rate", .05},
              {"yield", 0.},  {"volatility", .2}, {"right", "call"}};
    auto c = european_option(o);
    near(c["price"], 10.450583572185565, 1e-10, "Call benchmark");
    o["right"] = "put";
    auto p = european_option(o);
    near(p["price"], 5.573526022256971, 1e-10, "Put benchmark");
    near(c["price"].get<double>() - p["price"].get<double>(), 100 - 100 * std::exp(-.05), 1e-10,
         "Put-call parity");
    o["right"] = "call";
    auto price = [&](json x) { return european_option(x)["price"].get<double>(); };
    auto up = o, dn = o;
    up["spot"] = 100.001;
    dn["spot"] = 99.999;
    near((price(up) - price(dn)) / .002, c["delta"], 1e-8, "Delta finite difference");
    near((price(up) - 2 * price(o) + price(dn)) / 1e-6, c["gamma"], 1e-7,
         "Gamma finite difference");
    up = o;
    dn = o;
    up["volatility"] = .20001;
    dn["volatility"] = .19999;
    near((price(up) - price(dn)) / .00002 * .01, c["vega_1pp"], 1e-8, "Vega units");
    up = o;
    dn = o;
    up["years"] = 1.00001;
    dn["years"] = .99999;
    near(-(price(up) - price(dn)) / .00002 / 365, c["theta_day"], 1e-9, "Theta calendar sign");
    o["style"] = "American";
    rejects([&] { european_option(o); }, "Reject American");
    o.erase("style");
    o["years"] = 0;
    o["spot"] = 110;
    near(price(o), 10, 1e-12, "Expiry intrinsic");
    check(european_option(o)["delta"].is_null(), "Expiry undefined Greek");
    json stock = {{"id", "s"},    {"type", "STK"},    {"underlying", "X"},
                  {"spot", 100.}, {"quantity", 100.}, {"multiplier", 1.}};
    json book = {{"currency", "USD"},
                 {"positions", json::array({stock})},
                 {"scenario", {{"spot", -.1}, {"volatility", 0.}, {"days", 0}}}};
    auto b = hedge_book(book);
    near(b["scenario_pnl"], -1000, 1e-10, "Stock scenario");
    near(b["underlyings"]["X"]["delta_neutral_stock_units"], -100, 1e-10, "Hedge sign");
    book["positions"][0]["type"] = "FUT";
    book["positions"][0]["quantity"] = -2;
    book["positions"][0]["multiplier"] = 50;
    b = hedge_book(book);
    near(b["market_value"], 0, 1e-12, "Futures funded value");
    near(b["scenario_pnl"], 1000, 1e-10, "Short futures pnl");
    book["positions"][0]["currency"] = "EUR";
    rejects([&] { hedge_book(book); }, "Cross-currency prohibited");
    json raw = json::array({{{"pages", 1}},
                            json::array({{{"countryiso3code", "USA"},
                                          {"indicator", {{"id", "NY.GDP.MKTP.CD"}}},
                                          {"date", "2024"},
                                          {"value", 30000000000000.}}})});
    int64_t time = 1767225600000;
    auto data = worldbank_import(raw, "USA", "gdp", time);
    check(macro_view(data, "2025-12-31")["observations"].empty(), "No macro lookahead");
    check(macro_view(data, "2026-01-01")["observations"].size() == 1, "Macro retrieval visibility");
    check(data["macro_observations"][0]["known_at"].is_null(), "No invented release date");
    raw[1][0]["value"] = nullptr;
    check(worldbank_import(raw, "USA", "gdp", time)["macro_observations"][0]["value"].is_null(),
          "Missing macro value");
    raw[1][0]["value"] = 0;
    check(worldbank_import(raw, "USA", "gdp", time)["macro_observations"][0]["value"] == 0,
          "Zero macro value");
    rejects([&] { worldbank_import(raw, "CHN", "gdp", time); }, "Country binding");
    rejects([&] { public_fetch("test", "https://www.federalreserve.gov/feeds/press_all.xml"); },
            "Test external fetch blocked");
    std::string rss =
        "<rss><channel><item><title>Test &amp; "
        "verification</title><link>https://www.federalreserve.gov/test.htm</link><pubDate>Thu, 01 "
        "Jan 2026 12:00:00 GMT</pubDate><description>Test</description></item></channel></rss>";
    auto news = fed_news_import(rss);
    check(news["news"].size() == 1, "RSS parse");
    check(news["news"][0]["title"] == "Test & verification", "RSS escaped content");
    rejects(
        [&] { fed_news_import("<!DOCTYPE rss [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>" + rss); },
        "Reject XML DTD");
    json summary;
    auto field = [&](double n) {
        return json{{"amount", n}, {"currency", "USD"}, {"isNull", false}, {"timestamp", time}};
    };
    summary["netliquidation"] = field(100000);
    summary["excessliquidity"] = field(5000);
    auto m = broker_margin(summary, "TEST", time);
    check(m["status"] == "low-buffer", "Margin local warning");
    near(m["liquidity_ratio"], .05, 1e-12, "Margin ratio");
    summary["excessliquidity"]["currency"] = "EUR";
    check(broker_margin(summary, "TEST", time)["liquidity_ratio"].is_null(),
          "No mixed currency ratio");
    summary["excessliquidity"] = field(0);
    check(broker_margin(summary, "TEST", time)["status"] == "critical", "Explicit zero critical");
    summary["excessliquidity"]["isNull"] = true;
    check(broker_margin(summary, "TEST", time)["status"] == "unknown", "Null is not zero");
    summary["excessliquidity"] = field(5000);
    check(broker_margin(summary, "TEST", time + 120001)["status"] == "unknown",
          "Stale margin unknown");
    summary["excessliquidity"]["currency"] = 1.1;
    check(broker_margin(summary, "TEST", time)["status"] == "unknown",
          "Malformed currency unknown");
    summary = json{{"excessliquidity-s", field(100)}};
    check(broker_margin(summary, "TEST", time)["status"] == "unknown", "No segment sum");
    auto dividend = o;
    dividend["years"] = 1.;
    dividend["spot"] = 100.;
    dividend["yield"] = .03;
    dividend["right"] = "call";
    auto dc = european_option(dividend);
    dividend["right"] = "put";
    auto dp = european_option(dividend);
    near(dc["price"].get<double>() - dp["price"].get<double>(),
         100 * std::exp(-.03) - 100 * std::exp(-.05), 1e-10, "Dividend parity");
    near(dc["delta"].get<double>() - dp["delta"].get<double>(), std::exp(-.03), 1e-12,
         "Dividend delta parity");
    dividend["id"] = "option";
    dividend["type"] = "OPT";
    dividend["underlying"] = "X";
    dividend["quantity"] = -1.;
    dividend["multiplier"] = 100.;
    json options = {{"currency", "USD"},
                    {"positions", json::array({dividend})},
                    {"scenario", {{"spot", .1}, {"volatility", 0.}, {"days", 365}}}};
    auto expiry = hedge_book(options);
    near(expiry["scenario_pnl"], dp["price"].get<double>() * 100, 1e-8,
         "Short put expiry full repricing");
    options["scenario"]["days"] = 0;
    options["scenario"]["volatility"] = -.3;
    rejects([&] { hedge_book(options); }, "Invalid shocked volatility rejected");
    auto revision = worldbank_import(raw, "USA", "gdp", time + 86400000);
    auto merged = merge_data(data, revision);
    check(macro_view(merged, "2026-01-02")["history"].size() == 2, "Revision history retained");
    check(macro_view(merged, "2026-01-02")["observations"][0]["value"] == 0,
          "Latest retrieval selected");
    if(argc==3) {
        auto read=[](const char*path){std::ifstream f(path);require(bool(f),"Fixture missing");return std::string(std::istreambuf_iterator<char>(f),{});};
        auto wb=worldbank_import(json::parse(read(argv[1])),"USA","gdp",epoch_ms());auto feed=fed_news_import(read(argv[2]));
        auto verified=merge_data(merge_data(empty_data(),wb),feed);
        check(!verified["macro_observations"].empty()&&!verified["news"].empty(),"Actual official source payloads validated");
        std::cout<<"Official payload rows: macro="<<verified["macro_observations"].size()<<", news="<<verified["news"].size()<<"\n";
    }
    std::cout << count << " release assertions passed\n";
}
