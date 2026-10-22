#pragma once
#include "api.hpp"
#include "widgets.hpp"
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>
#include <sys/stat.h>
class Terminal {
    struct Job {
        int revision;
        bool global;
        bool durable;
        std::function<json(LocalApi &)> work;
        std::function<void(const json &)> done;
        std::function<void(const std::string &)> failed;
    };
    struct Completion {
        Terminal *owner;
        std::shared_ptr<std::atomic<bool>> life;
        Job job;
        json data;
        std::string error;
    };
    std::atomic<bool> stopping{false};
    std::shared_ptr<std::atomic<bool>> life = std::make_shared<std::atomic<bool>>(true);
    LocalApi api;
    std::mutex mutex;
    std::condition_variable cv;
    std::queue<Job> queue;
    std::thread worker;
    std::atomic<int> revision{0}, pending{0};
    GtkWidget *window, *content, *status, *instrumentBox, *dateBox, *nav;
    std::vector<GtkWidget *> navButtons;
    json instruments = json::array(), brokerAccounts = json::array();
    std::string selected, contract;
    int pageIndex = 0;
    guint monitor = 0;
    int64_t deadline = 0;
    bool brokerBusy = false, testMode = false, connected = false;
    int testPage = -1;
    std::string snapshot;
    bool testFailed = false;
    json lastHedge;
    GtkListStore *hedgeStore = nullptr;
    static std::string today() {
        std::time_t now = std::time(nullptr);
        std::tm t{};
        gmtime_r(&now, &t);
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d", &t);
        return b;
    }
    std::string query() const {
        return "as_of=" + LocalApi::encode(value(dateBox));
    }
    std::string researchPath() const {
        return "/api/research?instrument=" + LocalApi::encode(selected) + "&" + query();
    }
    void footer(const std::string &s) {
        gtk_label_set_text(GTK_LABEL(status), s.c_str());
    }
    void request(std::function<json(LocalApi &)> work, std::function<void(const json &)> done,
                 std::function<void(const std::string &)> failed = {}, bool global = false,
                 bool durable = false) {
        footer("正在连接本地 C++ 服务…");
        pending++;
        {
            std::lock_guard lock(mutex);
            queue.push({revision.load(), global, durable, std::move(work), std::move(done),
                        std::move(failed)});
        }
        cv.notify_one();
    }
    void get(const std::string &path, std::function<void(const json &)> done) {
        request([path](LocalApi &a) { return a.get(path); }, std::move(done));
    }
    void post(const std::string &path, const json &body, std::function<void(const json &)> done) {
        bool durable = path == "/api/import" || path == "/api/notes" || path == "/api/valuations" ||
                       path == "/api/hedge/models" || path == "/api/sec/fetch" ||
                       path == "/api/market/daily" || path == "/api/news/fetch" ||
                       path == "/api/macro/fetch" || path == "/api/ibkr/settings" ||
                       path == "/api/ibkr/prepare";
        request([path, body](LocalApi &a) { return a.post(path, body); }, std::move(done), {},
                false, durable);
    }
    void show() {
        gtk_widget_show_all(content);
    }
    void title(const std::string &eyebrow, const std::string &name, const std::string &sub) {
        add(content, label(eyebrow, "eyebrow"));
        add(content, label(name, "page-title"));
        add(content, label(sub, "muted"));
    }
    void stopMonitor() {
        if (monitor) {
            g_source_remove(monitor);
            monitor = 0;
        }
        deadline = 0;
    }
    std::string chooseFile(bool save, const std::string &title) {
        auto dialog = gtk_file_chooser_dialog_new(
            title.c_str(), GTK_WINDOW(window),
            save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, "取消",
            GTK_RESPONSE_CANCEL, save ? "保存" : "打开", GTK_RESPONSE_ACCEPT, nullptr);
        if (save)
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), true);
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            auto p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (p) {
                path = p;
                g_free(p);
            }
        }
        gtk_widget_destroy(dialog);
        return path;
    }
    void uri(const std::string &u) {
        if (!(u.starts_with("https://") || u.starts_with("http://")))
            throw std::runtime_error("仅允许 HTTP/HTTPS 来源链接");
        GError *error = nullptr;
        if (!gtk_show_uri_on_window(GTK_WINDOW(window), u.c_str(), GDK_CURRENT_TIME, &error)) {
            std::string m = error ? error->message : "无法打开授权页面";
            g_clear_error(&error);
            throw std::runtime_error(m);
        }
    }
    void stats(GtkWidget *parent, const std::vector<std::pair<std::string, json>> &items) {
        auto grid = gtk_grid_new();
        gtk_grid_set_column_homogeneous(GTK_GRID(grid), true);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
        int n = 0;
        for (auto &[name, v] : items) {
            auto frame = gtk_frame_new(nullptr);
            auto b = box();
            gtk_container_set_border_width(GTK_CONTAINER(b), 18);
            add(b, label(name, "muted"));
            add(b, label(text(v), "metric"));
            gtk_container_add(GTK_CONTAINER(frame), b);
            int index = n++;
            gtk_grid_attach(GTK_GRID(grid), frame, index % 4, index / 4, 1, 1);
        }
        add(parent, grid);
    }
    void reloadInstruments() {
        request([](LocalApi &a) { return a.get("/api/instruments"); },
                [this](const json &rows) {
                    auto keep = selected;
                    instruments = rows;
                    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(instrumentBox));
                    for (auto &i : rows)
                        gtk_combo_box_text_append(
                            GTK_COMBO_BOX_TEXT(instrumentBox), i["id"].get<std::string>().c_str(),
                            (field(i, "symbol") + " · " + field(i, "name")).c_str());
                    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(instrumentBox), keep.c_str()) &&
                        !rows.empty())
                        gtk_combo_box_set_active(GTK_COMBO_BOX(instrumentBox), 0);
                    footer("研究标的列表已更新");
                },
                {}, true);
    }
    void overview();
    void research();
    void valuation();
    void risk();
    void news();
    void data();
    void hedge();
    void margin();
    void macro();
    void gatewayCheck(GtkWidget *line, GtkWidget *prepare, GtkWidget *results);
    json hedgeInputs(GtkWidget *ccy, GtkWidget *spotShock, GtkWidget *volShock, GtkWidget *days);
    void hedgeResult(GtkWidget *result, const json &r);
    void preview();
    void render(int page) {
        stopMonitor();
        revision++;
        brokerBusy = false;
        contract.clear();
        brokerAccounts = json::array();
        pageIndex = page;
        clear(content);
        for (size_t i = 0; i < navButtons.size(); ++i) {
            auto ctx = gtk_widget_get_style_context(navButtons[i]);
            if (static_cast<int>(i) == page)
                gtk_style_context_add_class(ctx, "selected");
            else
                gtk_style_context_remove_class(ctx, "selected");
        }
        switch (page) {
        case 0:
            overview();
            break;
        case 1:
            research();
            break;
        case 2:
            valuation();
            break;
        case 3:
            risk();
            break;
        case 4:
            news();
            break;
        case 5:
            data();
            break;
        case 6:
            hedge();
            break;
        case 7:
            margin();
            break;
        case 8:
            macro();
            break;
        }
        show();
    }

  public:
    Terminal(int port, bool selftest, const std::string &image)
        : api(port, selftest ? "test" : "user", stopping), testMode(selftest), snapshot(image) {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "ReachTerminal · 原生桌面终端");
        gtk_window_set_default_size(GTK_WINDOW(window), 1440, 940);
        gtk_widget_set_size_request(window, 1050, 650);
        auto shell = box(true, 0);
        gtk_container_add(GTK_CONTAINER(window), shell);
        auto rail = box(false, 16);
        gtk_widget_set_size_request(rail, 210, -1);
        gtk_style_context_add_class(gtk_widget_get_style_context(rail), "rail");
        gtk_container_set_border_width(GTK_CONTAINER(rail), 18);
        add(shell, rail);
        add(rail, label("R ↗  REACH", "brand"));
        add(rail, label("TERMINAL / NATIVE C++", "eyebrow"));
        add(rail, label("投资研究工作区", "muted"));
        const std::vector<std::string> names = {"◫  组合总览",   "⌕  公司研究",   "ƒ  估值工作台",
                                                "◇  风险与策略", "≋  新闻与研究", "▤  数据中心",
                                                "Δ  衍生品对冲", "◴  保证金监控", "◎  宏观数据"};
        for (size_t i = 0; i < names.size(); ++i) {
            auto b = button(names[i], [this, i] {
                if (connected)
                    render(static_cast<int>(i));
            });
            navButtons.push_back(b);
            add(rail, b);
        }
        auto space = box();
        add(rail, space, true);
        add(rail, label("LOCAL · DEVICE ISOLATED", "eyebrow"));
        add(rail, label("C++ / GTK / PostgreSQL\nv0.1.0 · 只读券商接入", "muted"));
        auto main = box(false, 0);
        add(shell, main, true);
        auto top = box(true);
        gtk_container_set_border_width(GTK_CONTAINER(top), 16);
        add(main, top);
        add(top, label(selftest ? "TEST / SYNTHETIC" : "LOCAL / PRIVATE", "eyebrow"));
        instrumentBox = gtk_combo_box_text_new();
        gtk_widget_set_hexpand(instrumentBox, true);
        add(top, instrumentBox, true);
        dateBox = entry(today());
        gtk_widget_set_size_request(dateBox, 135, -1);
        add(top, label("分析日期"));
        add(top, dateBox);
        add(top, button("刷新", [this] {
                if (connected)
                    render(pageIndex);
            }));
        auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        content = box(false, 18);
        gtk_container_set_border_width(GTK_CONTAINER(content), 24);
        gtk_container_add(GTK_CONTAINER(scroll), content);
        add(main, scroll, true);
        status = label("正在连接本地服务…", "muted");
        gtk_widget_set_margin_start(status, 20);
        gtk_widget_set_margin_bottom(status, 10);
        add(main, status);
        on(instrumentBox, "changed", [this] {
            selected = chosen(instrumentBox);
            if (connected && (pageIndex == 1 || pageIndex == 2 || pageIndex == 4))
                render(pageIndex);
        });
        on(window, "destroy", [this] {
            *life = false;
            stopping = true;
            stopMonitor();
            cv.notify_all();
            gtk_main_quit();
        });
        worker = std::thread([this] {
            while (!stopping) {
                Job j;
                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock, [this] { return stopping || !queue.empty(); });
                    if (stopping)
                        break;
                    j = std::move(queue.front());
                    queue.pop();
                }
                if (!j.global && !j.durable && j.revision != revision.load()) {
                    pending--;
                    continue;
                }
                auto result = new Completion{this, life, std::move(j), {}, {}};
                try {
                    result->data = result->job.work(api);
                } catch (const std::exception &e) {
                    result->error = e.what();
                }
                g_idle_add_full(
                    G_PRIORITY_DEFAULT,
                    +[](gpointer p) -> gboolean {
                        std::unique_ptr<Completion> r(static_cast<Completion *>(p));
                        if (!*r->life)
                            return G_SOURCE_REMOVE;
                        auto t = r->owner;
                        t->pending--;
                        if (!r->job.global && r->job.revision != t->revision.load())
                            return G_SOURCE_REMOVE;
                        try {
                            if (!r->error.empty()) {
                                t->footer(r->error);
                                if (r->job.failed)
                                    r->job.failed(r->error);
                                if (t->testMode)
                                    t->testFailed = true;
                            } else {
                                t->footer("本地工作区 · 数据来源和分析日期可追溯");
                                r->job.done(r->data);
                                t->show();
                            }
                        } catch (const std::exception &e) {
                            t->footer(e.what());
                            if (t->testMode)
                                t->testFailed = true;
                        }
                        return G_SOURCE_REMOVE;
                    },
                    result, nullptr);
            }
        });
        request(
            [](LocalApi &a) {
                a.connect();
                return a.get("/api/instruments");
            },
            [this](const json &rows) {
                instruments = rows;
                for (auto &i : rows)
                    gtk_combo_box_text_append(
                        GTK_COMBO_BOX_TEXT(instrumentBox), i["id"].get<std::string>().c_str(),
                        (field(i, "symbol") + " · " + field(i, "name")).c_str());
                if (!rows.empty())
                    gtk_combo_box_set_active(GTK_COMBO_BOX(instrumentBox), 0);
                connected = true;
                render(0);
            },
            [this](const std::string &e) {
                clear(content);
                title("LOCAL CONNECTION", "后端连接失败", e);
                add(content, label("请通过 scripts/desktop.sh 启动。桌面界面仅连接本机 "
                                   "127.0.0.1，不连接其他设备的网关。"));
                add(content, button("重试连接", [this] {
                        request(
                            [](LocalApi &a) {
                                a.connect();
                                return a.get("/api/instruments");
                            },
                            [this](const json &v) {
                                instruments = v;
                                reloadInstruments();
                                connected = true;
                                render(0);
                            },
                            {}, true);
                    }));
                show();
            },
            true);
        if (testMode)
            g_timeout_add(
                100,
                +[](gpointer p) -> gboolean {
                    auto t = static_cast<Terminal *>(p);
                    if (t->testFailed) {
                        gtk_widget_destroy(t->window);
                        return G_SOURCE_REMOVE;
                    }
                    if (!t->connected || t->pending.load() != 0)
                        return G_SOURCE_CONTINUE;
                    if (++t->testPage < 9) {
                        t->render(t->testPage);
                        return G_SOURCE_CONTINUE;
                    }
                    if (t->testPage == 9) {
                        t->render(6);
                        return G_SOURCE_CONTINUE;
                    }
                    t->preview();
                    gtk_widget_destroy(t->window);
                    return G_SOURCE_REMOVE;
                },
                this);
    }
    ~Terminal() {
        *life = false;
        stopping = true;
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }
    void run() {
        gtk_widget_show_all(window);
        gtk_main();
    }
    bool failed() const {
        return testFailed;
    }
};
