#include "window.hpp"
void Terminal::overview() {
    title("PORTFOLIO / USD BASE", "组合总览",
          "现金股票 FIFO · 策略归属 · 历史汇率 · 日末资金流近似收益");
    get("/api/portfolio?" + query(), [this](const json &p) {
        stats(content, {{"净资产 · USD", p.value("nav", json(nullptr))},
                        {"经济盈亏 · USD", p.value("economic_pnl", json(nullptr))},
                        {"股票市值", p.value("market_value", json(nullptr))},
                        {"现金余额", p.value("cash_value", json(nullptr))}});
        auto b = panel(content, "组合表现");
        chart(b, p.value("curve", json::array()), "return_index", "RETURN INDEX · BASE 100");
        b = panel(content, "策略持仓");
        std::vector<std::vector<std::string>> rows;
        for (auto &r : p["positions"])
            rows.push_back({field(r, "symbol"), field(r, "strategy"), field(r, "quantity"),
                            field(r, "cost_local"), field(r, "market_value_base"),
                            field(r, "realized_base"), field(r, "unrealized_base")});
        table(b, {"标的", "策略", "数量", "本币成本", "市值 · USD", "已实现盈亏", "未实现盈亏"},
              rows);
        auto note = panel(content, "核算质量");
        add(note,
            label(p.value("complete", false) ? "必要估值数据齐全；历史是否完整仍需经纪商对账。"
                                             : "估值数据不完整；缺失金额不以零替代。",
                  "muted"));
        for (auto &w : p.value("warnings", json::array()))
            add(note, label(text(w)));
        b = panel(content, "现金");
        rows.clear();
        for (auto &r : p["cash"])
            rows.push_back({field(r, "account"), field(r, "currency"), field(r, "amount"),
                            field(r, "base_value")});
        table(b, {"账户", "币种", "余额", "折算 · USD"}, rows, 150);
    });
}
void Terminal::research() {
    title("FUNDAMENTALS / POINT IN TIME", "公司研究",
          "按披露日期查看财报和估值指标；双击财务事实可在下方查看来源。");
    if (selected.empty()) {
        add(content, label("先在数据中心导入或注册研究标的。"));
        return;
    }
    get(researchPath(), [this](const json &r) {
        auto b = panel(content, field(r["instrument_info"], "name"));
        keyValues(b, r["metrics"]);
        b = panel(content, "原始日线 · 未复权");
        auto bars = r.value("bars", json::array());
        std::sort(bars.begin(), bars.end(), [](auto &a, auto &b) {
            return a.value("date", std::string()) < b.value("date", std::string());
        });
        chart(b, bars, "close", "DAILY CLOSE · " + field(r["instrument_info"], "currency"));
        b = panel(content, "当前分析日期可用的财务事实");
        auto facts = r.value("selected", json::array());
        std::vector<std::vector<std::string>> rows;
        for (auto &f : facts)
            rows.push_back({field(f, "concept"), field(f, "value"), field(f, "unit"),
                            field(f, "period_end"), field(f, "filed"), field(f, "source")});
        auto view = table(b, {"项目", "值", "单位", "报告期", "披露日期", "来源"}, rows, 320);
        auto detail = label("选择一项事实查看原始来源。", "muted");
        gtk_label_set_selectable(GTK_LABEL(detail), true);
        add(b, detail);
        activateRow(view, [facts, detail](int i) {
            if (i >= 0 && static_cast<size_t>(i) < facts.size())
                gtk_label_set_text(
                    GTK_LABEL(detail),
                    (field(facts[i], "source_url") + "\nAccession: " + field(facts[i], "accession"))
                        .c_str());
        });
        b = panel(content, "财务披露历史 · 包含季度与重述");
        rows.clear();
        for (auto &f : r.value("history", json::array()))
            rows.push_back({field(f, "concept"), field(f, "value"), field(f, "unit"),
                            field(f, "period_kind"), field(f, "period_end"), field(f, "filed"),
                            field(f, "source")});
        table(b, {"项目", "值", "单位", "期间类型", "报告期", "披露日期", "来源"}, rows, 240);
    });
    auto peerPanel = panel(content, "同业比较 · 同一分析日期");
    get("/api/peers?" + query(), [peerPanel](const json &rows) {
        std::vector<std::vector<std::string>> cells;
        for (auto &p : rows) {
            auto i = p["instrument"], m = p["metrics"];
            cells.push_back({field(i, "symbol"), field(i, "currency"), field(m, "pe"),
                             field(m, "pb"), field(m, "fcf_yield"), field(m, "net_margin")});
        }
        table(peerPanel, {"标的", "币种", "市盈率", "市净率", "现金流收益率", "净利率"}, cells,
              170);
    });
}
void Terminal::valuation() {
    title("VALUATION / ASSUMPTIONS", "估值工作台",
          "自由现金流假设由你填写；默认示例不来自公司财报。金融企业不适用此模型。");
    if (selected.empty()) {
        add(content, label("请先选择研究标的。"));
        return;
    }
    auto b = panel(content, "现金流折现 · FCFF");
    auto fcff = number(1e8, .01), growth = number(5, -94, 100), wacc = number(10, .001, 100),
         terminal = number(2, -49, 99), debt = number(0), shares = number(1e8, .01),
         years = number(5, 1, 20, 1), ccy = entry("USD"), name = entry("基准估值");
    for (auto &i : instruments)
        if (i["id"] == selected)
            gtk_entry_set_text(GTK_ENTRY(ccy), field(i, "currency").c_str());
    gtk_editable_set_editable(GTK_EDITABLE(ccy), false);
    form(b, {{"基期 FCFF · 本币", fcff},
             {"预测增长率 %", growth},
             {"资本成本 %", wacc},
             {"永续增长率 %", terminal},
             {"净债务 · 本币", debt},
             {"普通股股数", shares},
             {"预测年数", years},
             {"模型币种", ccy},
             {"版本名称", name}});
    auto result = panel(content, "估值结果");
    auto assumptions = [=, this] {
        std::string sector;
        for (auto &i : instruments)
            if (i["id"] == selected)
                sector = i.value("sector", std::string());
        return json{{"fcff", numeric(fcff)},
                    {"growth", numeric(growth) / 100},
                    {"wacc", numeric(wacc) / 100},
                    {"terminal_growth", numeric(terminal) / 100},
                    {"net_debt", numeric(debt)},
                    {"shares", numeric(shares)},
                    {"years", static_cast<int>(numeric(years))},
                    {"currency", value(ccy)},
                    {"sector", sector}};
    };
    auto calculate = [=, this] {
        auto a = assumptions();
        request(
            [a](LocalApi &api) {
                json out = {{"base", api.post("/api/dcf", a)}, {"rows", json::array()}};
                double w = a["wacc"], g = a["terminal_growth"];
                for (double dw : {-0.01, 0.0, 0.01}) {
                    json row = json::array({(w + dw) * 100});
                    for (double dg : {-0.005, 0.0, 0.005}) {
                        if (w + dw <= 0 || w + dw <= g + dg) {
                            row.push_back(nullptr);
                            continue;
                        }
                        auto v = a;
                        v["wacc"] = w + dw;
                        v["terminal_growth"] = g + dg;
                        row.push_back(api.post("/api/dcf", v)["per_share"]);
                    }
                    out["rows"].push_back(row);
                }
                out["growth"] = g * 100;
                return out;
            },
            [=, this](const json &out) {
                auto r = out["base"];
                clear(result);
                stats(result, {{"每股价值 · " + field(r, "currency"), r["per_share"]},
                               {"企业价值", r["enterprise_value"]},
                               {"股权价值", r["equity_value"]},
                               {"终值占比", r["terminal_share"]}});
                add(result, label("敏感性 · 每股价值（无效假设显示为空）", "section-title"));
                double g = out["growth"];
                std::vector<std::vector<std::string>> rows;
                for (auto &row : out["rows"])
                    rows.push_back({text(row[0]) + "%", text(row[1]), text(row[2]), text(row[3])});
                table(result,
                      {"WACC / 永续增长", text(g - .5) + "%", text(g) + "%", text(g + .5) + "%"},
                      rows, 150);
            });
    };
    add(b, button("计算估值与敏感性", calculate));
    if (testMode)
        calculate();
    add(b, button("保存估值版本", [=, this] {
            post("/api/valuations",
                 {{"instrument", selected},
                  {"date", value(dateBox)},
                  {"title", value(name)},
                  {"assumptions", assumptions()}},
                 [this](const json &) { footer("估值版本已保存"); });
        }));
    get(researchPath(), [this](const json &r) {
        auto b = panel(content, "历史估值版本");
        std::vector<std::vector<std::string>> rows;
        for (auto &v : r.value("valuations", json::array()))
            rows.push_back({field(v, "date"), field(v, "title"), field(v["result"], "per_share"),
                            field(v["result"], "currency")});
        table(b, {"日期", "方案", "每股价值", "币种"}, rows, 160);
    });
}
void Terminal::risk() {
    title("RISK / STRATEGY", "风险与策略",
          "现金股票与外汇联合冲击；衍生品和保证金使用各自工作区。");
    auto b = panel(content, "组合压力情景");
    auto equity = number(-10, -100, 100), fx = number(-5, -100, 100);
    form(b, {{"股票价格冲击 %", equity}, {"非美元兑美元冲击 %", fx}});
    auto result = box();
    add(b, result);
    add(b, button("重新估值", [=, this] {
            post("/api/stress",
                 {{"as_of", value(dateBox)},
                  {"equity_shock", numeric(equity) / 100},
                  {"fx_shock", numeric(fx) / 100}},
                 [this, result](const json &r) {
                     clear(result);
                     stats(result,
                           {{"情景损益 · USD", r["pnl"]}, {"冲击后净资产", r["nav_after"]}});
                 });
        }));
    get("/api/portfolio?" + query(), [this](const json &p) {
        for (auto pair : std::vector<std::pair<std::string, std::string>>{
                 {"行业敞口", "sector_exposure"}, {"币种敞口", "currency_exposure"}}) {
            auto b = panel(content, pair.first);
            auto values = p.value(pair.second, json::object());
            std::vector<std::vector<std::string>> rows;
            if (values.is_object())
                for (auto &[k, v] : values.items())
                    rows.push_back({k, text(v)});
            table(b, {"类别", "股票市值 · USD"}, rows, 150);
        }
        auto b = panel(content, "策略核算");
        auto strategies = p.value("strategies", json::array());
        if (strategies.is_object())
            keyValues(b, strategies);
        else if (strategies.is_array()) {
            std::vector<std::vector<std::string>> rows;
            for (auto &r : strategies)
                rows.push_back({field(r, "name"), field(r, "realized_base"),
                                field(r, "unrealized_base"), field(r, "market_value_base")});
            table(b, {"策略", "已实现盈亏", "未实现盈亏", "市值"}, rows);
        }
        add(b, label("近似收益指数最大回撤：" + field(p, "max_drawdown")));
    });
}
void Terminal::news() {
    title("INTELLIGENCE / RESEARCH JOURNAL", "新闻与研究",
          "官方新闻摘要、来源与公司投资论点；原文链接为可选外部阅读。");
    auto actions = box(true);
    add(content, actions);
    add(actions, button("获取美联储官方新闻", [this] {
            post("/api/news/fetch", json::object(), [this](const json &) { render(4); });
        }));
    auto items = panel(content, "新闻与公告");
    get("/api/news?" + query(), [this, items](const json &rows) {
        if (rows.empty())
            add(items, label("当前分析日期没有已导入的新闻。"));
        for (auto &n : rows) {
            auto item = panel(items, field(n, "title"));
            add(item, label(field(n, "date") + " · " + field(n, "source"), "eyebrow"));
            add(item, label(n.value("summary", std::string())));
            auto url = n.value("source_url", std::string());
            add(item, button("查看原文来源（外部）", [this, url] {
                    try {
                        uri(url);
                    } catch (const std::exception &e) {
                        footer(e.what());
                    }
                }));
        }
    });
    if (!selected.empty()) {
        auto b = panel(content, "投资论点");
        auto name = entry(""), body = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(body), GTK_WRAP_WORD_CHAR);
        gtk_widget_set_size_request(body, -1, 140);
        form(b, {{"标题", name}});
        add(b, body);
        add(b, button("保存研究笔记", [=, this] {
                auto buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(body));
                GtkTextIter a, z;
                gtk_text_buffer_get_bounds(buf, &a, &z);
                auto p = gtk_text_buffer_get_text(buf, &a, &z, false);
                std::string note = p;
                g_free(p);
                post("/api/notes",
                     {{"instrument", selected},
                      {"date", value(dateBox)},
                      {"title", value(name)},
                      {"body", note}},
                     [this](const json &) { render(4); });
            }));
        get(researchPath(), [b](const json &r) {
            for (auto &n : r.value("notes", json::array())) {
                add(b, label(field(n, "date") + " · " + field(n, "title"), "section-title"));
                add(b, label(field(n, "body")));
            }
        });
    }
}
void Terminal::gatewayCheck(GtkWidget *line, GtkWidget *prepare, GtkWidget *) {
    if (brokerBusy)
        return;
    brokerBusy = true;
    request([](LocalApi &a) { return a.post("/api/ibkr/connection"); },
            [=, this](const json &r) {
                brokerBusy = false;
                bool ready = r.value("market_ready", r.value("authenticated", false));
                std::string msg;
                if (r.value("competing", false))
                    msg = "检测到竞争行情会话，请核对其他设备或 TWS。";
                else if (r.value("initializing", false))
                    msg = "账户已登录，行情正在初始化并检查账户握手；请等待就绪。";
                else if (ready) {
                    msg = "登录成功：行情会话已认证，可以搜索标的并准备行情。";
                    stopMonitor();
                } else if (r.contains("sso_valid") && r["sso_valid"] == true)
                    msg = "账户登录成功；行情未认证。可点击“连接行情会话（不抢占）”。";
                else if (r.contains("sso_valid") && r["sso_valid"] == false)
                    msg = "账户登录尚未确认，请完成官方授权和双重验证。";
                else
                    msg = "状态未知：" + r.value("sso_error", std::string("账户验证暂未返回结果"));
                msg += "\n检测时间：" + epochText(r.value("checked_at_ms", json(nullptr))) +
                       "；账户验证时间：" + epochText(r.value("sso_checked_at_ms", json(nullptr))) +
                       "（最多每60秒验证）";
                gtk_label_set_text(GTK_LABEL(line), msg.c_str());
                gtk_widget_set_sensitive(prepare, ready);
            },
            [=, this](const std::string &e) {
                brokerBusy = false;
                gtk_label_set_text(GTK_LABEL(line), ("检测失败：" + e).c_str());
                gtk_widget_set_sensitive(prepare, false);
            });
}
void Terminal::data() {
    title("DATA / DEVICE-LOCAL CONNECTIVITY", "数据中心",
          "本机网关独立配置 · 导入导出 · SEC 财报 · 真实市场数据");
    auto b = panel(content, "IBKR 网关连接");
    auto line = label("正在检测本机连接…", "connection");
    auto actions = box(true);
    auto results = box();
    auto prepare = button("准备行情", [=, this] {
        post("/api/ibkr/prepare", json::object(), [line](const json &) {
            gtk_label_set_text(GTK_LABEL(line), "行情准备完成；首次快照可能仍需要预热。");
        });
    });
    gtk_widget_set_sensitive(prepare, false);
    auto watch = [=, this] {
        stopMonitor();
        deadline = g_get_monotonic_time() + 180000000;
        struct Monitor {
            Terminal *t;
            GtkWidget *line, *prepare, *results;
        };
        auto context = new Monitor{this, line, prepare, results};
        monitor = g_timeout_add_seconds_full(
            G_PRIORITY_DEFAULT, 5,
            +[](gpointer p) -> gboolean {
                auto m = static_cast<Monitor *>(p);
                auto t = m->t;
                if (t->pageIndex != 5 || t->stopping)
                    return G_SOURCE_REMOVE;
                if (g_get_monotonic_time() >= t->deadline) {
                    gtk_label_set_text(GTK_LABEL(m->line),
                                       "等待确认超时，不能据此判断登录失败。请手动重新检测。");
                    t->monitor = 0;
                    t->deadline = 0;
                    return G_SOURCE_REMOVE;
                }
                t->gatewayCheck(m->line, m->prepare, m->results);
                return G_SOURCE_CONTINUE;
            },
            context, +[](gpointer p) { delete static_cast<Monitor *>(p); });
    };
    get("/api/ibkr/settings", [=, this](const json &s) {
        auto port = number(s.value("port", 5000), 1024, 65535, 1), ca = gtk_text_view_new();
        gtk_widget_set_size_request(ca, -1, 90);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ca), GTK_WRAP_WORD_CHAR);
        add(b, label("设备：" + field(s, "device") + " · " + field(s, "scope") +
                         "。配置、网关和登录状态不可迁移。",
                     "muted"));
        form(b, {{"本机网关端口", port}});
        add(b, label("公开 CA 证书 "
                     "PEM（只填写公开证书，不填写密码或私钥；留空重新保存会清除现有自定义证书）",
                     "muted"));
        add(b, ca);
        add(b, button("保存本机连接设置", [=, this] {
                stopMonitor();
                revision++;
                brokerBusy = false;
                contract.clear();
                clear(results);
                auto buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ca));
                GtkTextIter a, z;
                gtk_text_buffer_get_bounds(buf, &a, &z);
                auto raw = gtk_text_buffer_get_text(buf, &a, &z, false);
                std::string pem = raw;
                g_free(raw);
                post("/api/ibkr/settings",
                     {{"port", static_cast<int>(numeric(port))}, {"ca_pem", pem}},
                     [=, this](const json &) {
                         gtk_label_set_text(GTK_LABEL(line),
                                            "连接设置已保存，请重新检测登录状态。");
                         gtk_widget_set_sensitive(prepare, false);
                     });
            }));
        if (s.contains("login_url") && s["login_url"].is_string()) {
            auto url = s["login_url"].get<std::string>();
            add(actions, button("IBKR 官方授权（外部）", [=, this] {
                    try {
                        uri(url);
                        watch();
                        gatewayCheck(line, prepare, results);
                    } catch (const std::exception &e) {
                        footer(e.what());
                    }
                }));
        } else
            add(actions, label("隔离环境：禁止打开真实授权页面"));
        add(b, actions);
        add(actions, button("检测状态", [=, this] { gatewayCheck(line, prepare, results); }));
        add(actions, button("连接行情（不抢占）", [=, this] {
                request([](LocalApi &a) { return a.post("/api/ibkr/connect-market"); },
                        [=, this](const json &) {
                            watch();
                            gatewayCheck(line, prepare, results);
                        },
                        [line](const std::string &e) {
                            gtk_label_set_text(GTK_LABEL(line), e.c_str());
                        });
            }));
        add(actions, prepare);
        add(actions, button("登出", [=, this] {
                stopMonitor();
                revision++;
                brokerBusy = false;
                contract.clear();
                brokerAccounts = json::array();
                clear(results);
                gtk_widget_set_sensitive(prepare, false);
                request(
                    [](LocalApi &a) { return a.post("/api/ibkr/logout"); },
                    [line](const json &) {
                        gtk_label_set_text(GTK_LABEL(line), "网关已确认退出；本地查询状态已清除。");
                    },
                    [line](const std::string &e) {
                        gtk_label_set_text(GTK_LABEL(line),
                                           ("本地查询已清除，但远端退出尚未确认：" + e).c_str());
                    },
                    false, true);
            }));
        add(b, line);
        gatewayCheck(line, prepare, results);
    });
    auto searchPanel = panel(content, "全球标的搜索 · 只读预览");
    auto term = entry("IBM"), by = combo({{"symbol", "证券代码"}, {"name", "公司名称"}});
    form(searchPanel, {{"搜索内容", term}, {"搜索方式", by}});
    add(searchPanel, button("搜索 IBKR 标的", [=, this] {
            post("/api/ibkr/search", {{"query", value(term)}, {"by_name", chosen(by) == "name"}},
                 [=, this](const json &rows) {
                     clear(results);
                     std::vector<std::vector<std::string>> cells;
                     for (auto &r : rows)
                         cells.push_back({field(r, "conid"), field(r, "symbol"),
                                          field(r, "companyName"), field(r, "companyHeader")});
                     auto view = table(results, {"合约 ID", "代码", "名称", "市场"}, cells);
                     add(results,
                         label("双击合约行获取详情。此查询不会写入持仓或价格账本。", "muted"));
                     activateRow(view, [=, this](int i) {
                         if (i < 0 || static_cast<size_t>(i) >= rows.size())
                             return;
                         contract = rows[i]["conid"].get<std::string>();
                         auto selectedConid = contract;
                         post("/api/ibkr/detail", {{"conid", selectedConid}},
                              [=, this](const json &r) {
                                  auto detail = panel(results, "已选合约");
                                  keyValues(detail, r);
                                  auto dataBox = box();
                                  add(detail, dataBox);
                                  add(detail, button("获取快照", [=, this] {
                                          post("/api/ibkr/snapshot", {{"conid", selectedConid}},
                                               [=](const json &s) {
                                                   clear(dataBox);
                                                   add(dataBox, label("行情可用性：" +
                                                                      field(s, "availability")));
                                                   std::vector<std::vector<std::string>> rows;
                                                   for (auto &v : s["fields"])
                                                       rows.push_back(
                                                           {field(v, "label"), field(v, "value")});
                                                   table(dataBox, {"指标", "原始值"}, rows);
                                               });
                                      }));
                                  add(detail, button("获取近月日线", [=, this] {
                                          post("/api/ibkr/history", {{"conid", selectedConid}},
                                               [=](const json &s) {
                                                   clear(dataBox);
                                                   auto bars = s.value("bars", json::array());
                                                   chart(dataBox, bars, "c", "IBKR DAILY PREVIEW");
                                                   std::vector<std::vector<std::string>> rows;
                                                   for (auto &v : bars)
                                                       rows.push_back({field(v, "timestamp"),
                                                                       field(v, "o"), field(v, "h"),
                                                                       field(v, "l"), field(v, "c"),
                                                                       field(v, "v")});
                                                   table(dataBox,
                                                         {"时间戳", "开盘", "最高", "最低", "收盘",
                                                          "成交量"},
                                                         rows);
                                               });
                                      }));
                              });
                     });
                 });
        }));
    add(searchPanel, results);
    auto registration = panel(content, "注册现金股票研究标的");
    auto identifier = entry(""), symbol = entry(""), company = entry(""), exchange = entry("XNYS"),
         country = entry("USA"), sector = entry("Technology"), currency = entry("USD"),
         sec = entry("");
    form(registration, {{"内部标识（唯一）", identifier},
                        {"证券代码", symbol},
                        {"公司名称", company},
                        {"交易所", exchange},
                        {"国家/地区", country},
                        {"行业", sector},
                        {"币种", currency},
                        {"SEC CIK（可选）", sec}});
    add(registration, button("注册研究标的", [=, this] {
            json i = {{"id", value(identifier)},     {"symbol", value(symbol)},
                      {"name", value(company)},      {"exchange", value(exchange)},
                      {"country", value(country)},   {"sector", value(sector)},
                      {"currency", value(currency)}, {"asset_class", "STK"}};
            if (!value(sec).empty())
                i["sec_cik"] = value(sec);
            post("/api/import", {{"instruments", json::array({i})}},
                 [this](const json &) { reloadInstruments(); });
        }));
    add(registration, button("刷新研究标的列表", [this] { reloadInstruments(); }));
    auto io = panel(content, "导入与导出");
    auto format = combo(
        {{"normalized", "标准 JSON"}, {"flex", "IBKR Flex XML"}, {"sec", "SEC CompanyFacts JSON"}});
    form(io, {{"文件格式", format}});
    add(io, button("选择文件并导入", [=, this] {
            auto path = chooseFile(false, "导入本地研究数据");
            if (path.empty())
                return;
            auto kind = chosen(format), instrument = selected;
            request(
                [path, kind, instrument](LocalApi &a) {
                    std::ifstream f(path, std::ios::binary);
                    if (!f)
                        throw std::runtime_error("无法打开导入文件");
                    std::string body((std::istreambuf_iterator<char>(f)), {});
                    if (body.size() > 32 * 1024 * 1024)
                        throw std::runtime_error("导入文件超过32MB");
                    auto endpoint = kind == "flex"  ? "/api/import/flex"
                                    : kind == "sec" ? "/api/import/sec?instrument=" +
                                                          LocalApi::encode(instrument)
                                                    : "/api/import";
                    return a.raw(endpoint, body,
                                 kind == "flex" ? "application/xml" : "application/json");
                },
                [this](const json &r) {
                    footer("导入完成：新增 " + field(r, "inserted") + "，重复 " +
                           field(r, "duplicates") + "。研究标的列表已刷新。");
                    reloadInstruments();
                },
                {}, true, true);
        }));
    add(io, button("导出完整工作区", [this] {
            auto path = chooseFile(true, "保存本地工作区备份");
            if (path.empty())
                return;
            request(
                [path](LocalApi &a) {
                    auto r = a.get("/api/export");
                    std::ofstream f(path);
                    if (!f)
                        throw std::runtime_error("无法写入导出文件");
                    if (chmod(path.c_str(), 0600) != 0)
                        throw std::runtime_error("无法保护备份文件权限");
                    f << r.dump(2);
                    f.flush();
                    if (!f)
                        throw std::runtime_error("导出写入失败");
                    return json::object();
                },
                [this](const json &) { footer("本地工作区备份已保存"); }, {}, true, true);
        }));
    auto sources = panel(content, "免费财报与日线");
    auto cik = entry("");
    for (auto &i : instruments)
        if (i["id"] == selected)
            gtk_entry_set_text(GTK_ENTRY(cik), i.value("sec_cik", std::string()).c_str());
    form(sources, {{"当前标的 SEC CIK", cik}});
    add(sources, button("抓取 SEC 财报", [=, this] {
            post("/api/sec/fetch", {{"instrument", selected}, {"cik", value(cik)}},
                 [this](const json &r) { footer("SEC 财报已导入：" + field(r, "inserted")); });
        }));
    add(sources, button("获取公开日线", [this] {
            post("/api/market/daily", {{"instrument", selected}},
                 [this](const json &r) { footer("日线已导入：" + field(r, "inserted")); });
        }));
    add(sources, label("服务端管理真实联系邮箱和个人数据密钥；IBM "
                       "支持公开试用。桌面程序不复制密钥或其他设备状态。",
                       "muted"));
}
json Terminal::hedgeInputs(GtkWidget *ccy, GtkWidget *shock, GtkWidget *vol, GtkWidget *days) {
    json positions = json::array();
    GtkTreeIter it;
    bool valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(hedgeStore), &it);
    while (valid) {
        std::vector<std::string> cells;
        for (int i = 0; i < 12; ++i) {
            gchar *p = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(hedgeStore), &it, i, &p, -1);
            cells.emplace_back(p ? p : "");
            g_free(p);
        }
        auto num = [&](int i) {
            size_t used = 0;
            double n = std::stod(cells[i], &used);
            if (used != cells[i].size() || !std::isfinite(n))
                throw std::runtime_error("假设持仓数值无效，请使用小数点：" + cells[i]);
            return n;
        };
        json p = {{"id", cells[0]}, {"type", cells[1]},   {"underlying", cells[2]},
                  {"spot", num(3)}, {"quantity", num(4)}, {"multiplier", num(5)}};
        if (cells[1] == "OPT") {
            p["right"] = cells[6];
            p["strike"] = num(7);
            p["years"] = num(8);
            p["volatility"] = num(9) / 100;
            p["rate"] = num(10) / 100;
            p["yield"] = num(11) / 100;
            p["style"] = "European";
        }
        positions.push_back(p);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(hedgeStore), &it);
    }
    return {{"currency", value(ccy)},
            {"positions", positions},
            {"scenario",
             {{"spot", numeric(shock) / 100},
              {"volatility", numeric(vol) / 100},
              {"days", numeric(days)}}}};
}
void Terminal::hedgeResult(GtkWidget *result, const json &r) {
    if (testMode && (std::abs(r["market_value"].get<double>() - 10602.79) > .02 ||
                     std::abs(r["scenario_pnl"].get<double>() + 353.39) > .02))
        throw std::runtime_error("Native hedge input mapping failed reference values");
    lastHedge = r;
    clear(result);
    add(result, label("模型结果 · " + field(r, "currency"), "section-title"));
    stats(result, {{"模型市值", r["market_value"]},
                   {"情景损益", r["scenario_pnl"]},
                   {"Vega / +1百分点", r["vega_1pp"]},
                   {"Theta / 日", r["theta_day"]}});
    std::vector<std::vector<std::string>> rows;
    for (auto &[u, g] : r["underlyings"].items())
        rows.push_back({u, field(g, "delta_units"), field(g, "delta_notional"),
                        field(g, "gamma_half_notional"), field(g, "delta_neutral_stock_units")});
    table(result, {"标的", "Delta 单位", "Delta 名义敞口", "Gamma × S² / 2", "理论股票对冲数量"},
          rows, 130);
    add(result, label("对冲数量仅中和当前模型 "
                      "Delta。情景采用全量重估；不计算券商保证金、期货基差或美式提前行权。",
                      "muted"));
}
void Terminal::hedge() {
    title("DERIVATIVES / SCENARIO LAB", "衍生品对冲",
          "独立单币种假设组合 · 原生可编辑持仓表 · 欧式期权与线性期货");
    auto b = panel(content, "组合假设");
    auto name = entry("保护性看跌示例"), ccy = entry("USD"), shock = number(-10, -99, 200),
         vol = number(5, -490, 490), days = number(7, 0, 3650, 1);
    form(b, {{"方案名称", name},
             {"币种", ccy},
             {"价格冲击 %", shock},
             {"波动率变化 · 百分点", vol},
             {"时间流逝 · 天", days}});
    add(b, label("以下为假设案例，不是账户持仓。双击表格单元格编辑；数值使用小数点。STK "
                 "股数、OPT/FUT 合约张数；真实合约乘数须自行核对。",
                 "muted"));
    std::vector<GType> types(12, G_TYPE_STRING);
    hedgeStore = gtk_list_store_newv(12, types.data());
    auto view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(hedgeStore));
    g_object_unref(hedgeStore);
    const std::vector<std::string> headers = {"编号",    "STK/OPT/FUT", "标的标识", "现价",
                                              "数量",    "乘数",        "call/put", "行权价",
                                              "期限/年", "波动率%",     "利率%",    "收益率%"};
    for (int i = 0; i < 12; ++i) {
        auto renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "editable", TRUE, nullptr);
        struct Edit {
            GtkListStore *store;
            int col;
        };
        auto edit = new Edit{hedgeStore, i};
        g_signal_connect_data(
            renderer, "edited",
            G_CALLBACK((+[](GtkCellRendererText *, gchar *path, gchar *value, gpointer p) {
                auto e = static_cast<Edit *>(p);
                GtkTreeIter it;
                if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(e->store), &it, path))
                    gtk_list_store_set(e->store, &it, e->col, value, -1);
            })),
            edit, +[](gpointer p, GClosure *) { delete static_cast<Edit *>(p); },
            G_CONNECT_DEFAULT);
        auto col = gtk_tree_view_column_new_with_attributes(headers[i].c_str(), renderer, "text", i,
                                                            nullptr);
        gtk_tree_view_column_set_resizable(col, true);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    }
    auto append = [this](const std::vector<std::string> &row) {
        GtkTreeIter it;
        gtk_list_store_append(hedgeStore, &it);
        for (int i = 0; i < 12; ++i)
            gtk_list_store_set(hedgeStore, &it, i, row[i].c_str(), -1);
    };
    append({"stock", "STK", "MODEL", "100", "100", "1", "call", "100", "0.5", "25", "4", "0"});
    append({"put", "OPT", "MODEL", "100", "1", "100", "put", "100", "0.5", "25", "4", "0"});
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, -1, 170);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    add(b, scroll);
    auto actions = box(true);
    add(b, actions);
    add(actions, button("增加持仓", [append] {
            append({"position-" + std::to_string(g_get_real_time()), "STK", "MODEL", "100", "100",
                    "1", "call", "100", "0.5", "25", "4", "0"});
        }));
    add(actions, button("删除选中持仓", [this, view] {
            GtkTreeIter it;
            auto sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
            if (gtk_tree_selection_get_selected(sel, nullptr, &it))
                gtk_list_store_remove(hedgeStore, &it);
        }));
    auto result = panel(content, "对冲与情景结果");
    auto analyze = [=, this] {
        try {
            auto assumptions = hedgeInputs(ccy, shock, vol, days);
            post("/api/hedge/analyze", assumptions,
                 [=, this](const json &r) { hedgeResult(result, r); });
        } catch (const std::exception &e) {
            footer(e.what());
            if (testMode)
                testFailed = true;
        }
    };
    add(actions, button("计算对冲与情景", analyze));
    add(actions, button("保存方案版本", [=, this] {
            try {
                post("/api/hedge/models",
                     {{"title", value(name)},
                      {"date", value(dateBox)},
                      {"assumptions", hedgeInputs(ccy, shock, vol, days)}},
                     [this](const json &) { footer("方案版本已保存，刷新后可以加载"); });
            } catch (const std::exception &e) {
                footer(e.what());
            }
        }));
    auto saved = panel(content, "历史方案 · 双击加载");
    get("/api/hedge/models?" + query(), [=, this](const json &models) {
        std::vector<std::vector<std::string>> rows;
        for (auto &m : models)
            rows.push_back({field(m, "date"), field(m, "title"), field(m["result"], "scenario_pnl"),
                            field(m["result"], "currency")});
        auto list = table(saved, {"日期", "方案", "情景损益", "币种"}, rows, 140);
        activateRow(list, [=, this](int i) {
            if (i < 0 || static_cast<size_t>(i) >= models.size())
                return;
            auto m = models[i], a = m["assumptions"];
            gtk_entry_set_text(GTK_ENTRY(name), field(m, "title").c_str());
            gtk_entry_set_text(GTK_ENTRY(ccy), field(a, "currency").c_str());
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(shock),
                                      a["scenario"]["spot"].get<double>() * 100);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(vol),
                                      a["scenario"]["volatility"].get<double>() * 100);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(days), a["scenario"]["days"].get<double>());
            gtk_list_store_clear(hedgeStore);
            for (auto &p : a["positions"])
                append({field(p, "id"), field(p, "type"), field(p, "underlying"), field(p, "spot"),
                        field(p, "quantity"), field(p, "multiplier"),
                        p.value("right", std::string("call")), field(p, "strike"),
                        field(p, "years"), text(p.value("volatility", .25) * 100),
                        text(p.value("rate", .04) * 100), text(p.value("yield", 0.) * 100)});
            hedgeResult(result, m["result"]);
        });
    });
    if (testMode)
        analyze();
}
void Terminal::margin() {
    title("BROKER / MARGIN MONITOR", "保证金监控",
          "券商报告汇总 · 本设备会话 · 账户及金额只在当前窗口预览");
    auto b = panel(content, "账户连接");
    auto accounts = gtk_combo_box_text_new();
    form(b, {{"本机账户", accounts}});
    auto result = panel(content, "保证金报告");
    add(b, button("读取本机账户列表", [=, this] {
            post("/api/ibkr/accounts", json::object(), [=, this](const json &rows) {
                brokerAccounts = rows;
                gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(accounts));
                for (auto &r : rows)
                    gtk_combo_box_text_append(
                        GTK_COMBO_BOX_TEXT(accounts), r["id"].get<std::string>().c_str(),
                        (field(r, "id") + " · " + field(r, "currency")).c_str());
                if (!rows.empty())
                    gtk_combo_box_set_active(GTK_COMBO_BOX(accounts), 0);
            });
        }));
    add(b, button("读取保证金", [=, this] {
            post("/api/ibkr/margin", {{"account", chosen(accounts)}}, [=](const json &r) {
                clear(result);
                std::string state = r.value("status", std::string());
                add(result, label(state == "critical"     ? "剩余流动性不足"
                                  : state == "low-buffer" ? "流动性缓冲偏低"
                                  : state == "reported"   ? "券商已报告"
                                                          : "数据不足或过期，状态未知",
                                  "section-title"));
                add(result, label("剩余流动性 / 净清算值：" + field(r, "liquidity_ratio")));
                std::vector<std::vector<std::string>> rows;
                for (auto &v : r["fields"])
                    rows.push_back({field(v, "label"), field(v, "value"), field(v, "currency"),
                                    epochText(v.value("broker_timestamp", json(nullptr))),
                                    v.value("stale", true) ? "过期 / 未知" : "近期"});
                table(result, {"券商字段", "值", "币种", "券商更新时间 · UTC", "时效"}, rows, 300);
            });
        }));
    add(b, label("最短刷新间隔10秒；超过120秒的券商时间戳标记为过期。10%"
                 "仅是本地提醒阈值，不推断强平价格；不叠加证券与商品分项。",
                 "muted"));
}
void Terminal::macro() {
    title("GLOBAL / MACRO OBSERVATORY", "宏观数据",
          "世界银行年度 GDP、实际增长和 CPI 通胀 · 保存抓取版本");
    auto b = panel(content, "官方数据");
    auto country = combo({{"USA", "美国"},
                          {"CHN", "中国"},
                          {"DEU", "德国"},
                          {"JPN", "日本"},
                          {"GBR", "英国"},
                          {"HKG", "中国香港"},
                          {"FRA", "法国"},
                          {"IND", "印度"},
                          {"BRA", "巴西"},
                          {"AUS", "澳大利亚"}}),
         indicator = combo(
             {{"gdp", "名义 GDP"}, {"growth", "实际 GDP 增速"}, {"inflation", "居民消费价格通胀"}});
    form(b, {{"国家 / 地区", country}, {"指标", indicator}});
    add(b, button("抓取年度数据", [=, this] {
            post("/api/macro/fetch",
                 {{"country", chosen(country)}, {"indicator", chosen(indicator)}},
                 [this](const json &) { render(8); });
        }));
    add(b, label("报告期不等于发布日期。历史发布日期未知，从实际抓取日期起允许历史分析使用；缺失值"
                 "保持为空。",
                 "muted"));
    get("/api/macro?" + query(), [this](const json &r) {
        auto b = panel(content, "年度观测");
        auto rows = r["observations"];
        std::vector<std::vector<std::string>> cells;
        std::vector<std::pair<std::string, std::string>> series;
        std::set<std::string> keys;
        for (auto &o : rows) {
            cells.push_back({field(o, "country"), field(o, "indicator"), field(o, "period_end"),
                             field(o, "value"), field(o, "unit"), field(o, "date")});
            auto key = field(o, "country") + "/" + field(o, "indicator");
            if (keys.insert(key).second)
                series.push_back({key, key});
        }
        table(b, {"国家", "指标", "报告期", "值", "单位", "抓取日期"}, cells, 300);
        auto selection = combo(series);
        form(b, {{"图表序列", selection}});
        auto plot = box();
        add(b, plot);
        auto draw = [=] {
            clear(plot);
            auto key = chosen(selection);
            json data = json::array();
            for (auto &o : rows)
                if (field(o, "country") + "/" + field(o, "indicator") == key)
                    data.push_back(o);
            std::sort(data.begin(), data.end(), [](auto &a, auto &b) {
                return a["period_end"].template get<std::string>() <
                       b["period_end"].template get<std::string>();
            });
            chart(plot, data, "value", key + " · GDP: trillion USD; growth/inflation: %",
                  key.ends_with("/gdp") ? 1e12 : 1);
            gtk_widget_show_all(plot);
        };
        on(selection, "changed", draw);
        draw();
    });
}
