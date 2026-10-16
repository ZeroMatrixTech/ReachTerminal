#pragma once
#include "environment.hpp"
#include "imports.hpp"
#include "margin.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <thread>

namespace reach {
inline std::string ibkr_conid(const json &v) {
    auto s = v.is_string() ? v.get<std::string>() : v.is_number_integer() ? v.dump() : "";
    require(std::regex_match(s, std::regex("[1-9][0-9]{0,11}")), "Invalid IBKR conid");
    return s;
}
inline json ibkr_settings(const json &v) {
    require(v.is_object(), "Settings must be object");
    for (auto it = v.begin(); it != v.end(); ++it)
        require(it.key() == "port" || it.key() == "ca_pem",
                "Only port and certificate are accepted; Gateway uses browser login, not API keys");
    require(v.contains("port") && v["port"].is_number_integer(), "Gateway port required");
    int port = v["port"].get<int>();
    require(port >= 1024 && port <= 65535, "Invalid Gateway port");
    auto pem = v.value("ca_pem", std::string());
    require(pem.size() <= 65536 &&
                (pem.empty() || (pem.starts_with("-----BEGIN CERTIFICATE-----") &&
                                 pem.find("PRIVATE KEY") == std::string::npos)),
            "Paste public CA certificate PEM only");
    return {{"port", port}, {"ca_pem", pem}};
}
inline json ibkr_search_rows(const json &raw) {
    require(raw.is_array() && raw.size() <= 1000, "Unexpected IBKR search response");
    json rows = json::array();
    for (auto &v : raw) {
        if (!v.is_object() || !v.contains("conid"))
            continue;
        if ((v["conid"] == "-1" || v["conid"] == -1) && v.value("secType", std::string()) == "BOND")
            continue; // Issuer group, not an addressable contract.
        json r = {{"conid", ibkr_conid(v["conid"])}};
        for (auto k : {"symbol", "companyName", "companyHeader", "description", "secType"})
            if (v.contains(k) && v[k].is_string())
                r[k] = v[k];
        r["sections"] = json::array();
        if (v.contains("sections") && v["sections"].is_array())
            for (auto &s : v["sections"])
                if (s.is_object() && s.contains("secType") && s["secType"].is_string())
                    r["sections"].push_back(s["secType"]);
        rows.push_back(r);
    }
    return rows;
}
inline json ibkr_snapshot(const json &raw, const std::string &conid) {
    require(raw.is_array(), "Unexpected IBKR snapshot response");
    const std::map<std::string, std::string> fields = {{"31", "最新成交价"},
                                                       {"84", "买价"},
                                                       {"86", "卖价"},
                                                       {"82", "价格变动"},
                                                       {"83", "变动百分比"},
                                                       {"7293", "52周最高价"},
                                                       {"7294", "52周最低价"},
                                                       {"7282", "90日平均成交量"},
                                                       {"7087", "30日历史波动率 · %"},
                                                       {"7671", "未来12月预期每股股息"},
                                                       {"7672", "过去12月股息字段（供应商口径）"}};
    json out = {{"conid", conid},
                {"availability", "unknown"},
                {"updated_ms", nullptr},
                {"fields", json::array()},
                {"pending", true}};
    for (auto &v : raw) {
        if (!v.is_object() || !v.contains("conid") || ibkr_conid(v["conid"]) != conid)
            continue;
        std::string a = v.value("6509", std::string());
        const std::map<char, std::string> types = {
            {'R', "实时"},   {'D', "延迟"},   {'Z', "冻结"},      {'Y', "延迟冻结"},
            {'N', "未订阅"}, {'i', "不完整"}, {'v', "VDR exempt"}};
        if (!a.empty() && types.contains(a[0]))
            out["availability"] = types.at(a[0]);
        if (v.contains("_updated") && v["_updated"].is_number_integer())
            out["updated_ms"] = v["_updated"];
        for (auto &[key, label] : fields) {
            json value = nullptr;
            if (v.contains(key) && (v[key].is_string() || v[key].is_number()))
                value = v[key];
            out["fields"].push_back({{"id", key}, {"label", label}, {"value", value}});
            if (!value.is_null())
                out["pending"] = false;
        }
        break;
    }
    return out;
}
inline std::string ibkr_device_id() {
    std::ifstream f("/etc/machine-id");
    std::string machine;
    std::getline(f, machine);
    require(std::regex_match(machine, std::regex("[0-9a-fA-F]{32}")),
            "Local machine identity unavailable; IBKR connection disabled");
    return hash("ReachTerminal/IBKR/device/v1:" + machine);
}
inline json ibkr_bound_settings(const json &stored, const std::string &device) {
    require(stored.is_object() && stored.value("device_id", std::string()) == device,
            "IBKR settings belong to another device; configure and log in on this device");
    auto v = stored;
    v.erase("device_id");
    return ibkr_settings(v);
}
inline std::string ibkr_http_error(const std::string &path, long status, const std::string &body) {
    auto endpoint = path.substr(0, path.find('?'));
    endpoint = std::regex_replace(endpoint, std::regex("/portfolio/[^/]+/summary"),
                                  "/portfolio/{account}/summary");
    endpoint = std::regex_replace(endpoint, std::regex("/iserver/contract/[^/]+/info"),
                                  "/iserver/contract/{conid}/info");
    std::string reason = "请核对请求参数、数据权限和网关状态";
    auto lower = body;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (status == 401 || status == 403 || lower.find("not authenticated") != std::string::npos ||
        lower.find("no bridge") != std::string::npos ||
        lower.find("init session") != std::string::npos)
        reason = "行情会话尚未就绪或已失效；账户网页登录可仍有效。请连接行情会话，等认证完成后再准"
                 "备行情";
    else if (lower.find("competing") != std::string::npos)
        reason = "存在其他行情会话，请在 IBKR 核对其他设备或 TWS 登录";
    else if (status == 400 && endpoint == "/iserver/accounts")
        reason = "行情会话可能仍在初始化。请等待登录状态更新为已认证后，再准备行情";
    else if (status == 400 && endpoint == "/iserver/auth/ssodh/init")
        reason = "网关未接受行情认证请求。请核对官方网关页面，完成登录和双重验证后重新连接";
    else if (status == 429)
        reason = "IBKR 请求频率超限，请稍后重试";
    return "IBKR " + endpoint + "（HTTP " + std::to_string(status) + "）：" + reason;
}
class Ibkr {
    json config = {{"port", 5000}, {"ca_pem", ""}};
    std::string file, device, mode;
    bool configured = false;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{nullptr, curl_easy_cleanup};
    std::chrono::steady_clock::time_point last{};
    bool prepared = false, market_accounts_ready = false;
    std::chrono::steady_clock::time_point last_bootstrap{};
    std::chrono::steady_clock::time_point market_init_until{};
    json sso_valid = nullptr;
    std::string sso_error;
    int64_t sso_checked = 0;
    std::chrono::steady_clock::time_point last_sso{};
    json account_cache = json::object();
    std::map<std::string, std::chrono::steady_clock::time_point> margin_times;
    void reset() {
        market_init_until = {};
        market_accounts_ready = false;
        last_bootstrap = {};
        sso_valid = nullptr;
        sso_error.clear();
        sso_checked = 0;
        prepared = false;
        account_cache = json::object();
        margin_times.clear();
        handle.reset(curl_easy_init());
        require(bool(handle), "curl init failed");
        curl_easy_setopt(handle.get(), CURLOPT_COOKIEFILE, "");
    }

  public:
    explicit Ibkr(const std::string &root, const std::string &runtime = "user")
        : device(ibkr_device_id()), mode(runtime) {
        file = root + (mode == "user" ? "/.local/devices/" : "/.local/" + mode + "/devices/") +
               device + "/ibkr.json";
        if (std::filesystem::exists(file)) {
            std::ifstream f(file);
            config = ibkr_bound_settings(json::parse(f), device);
            configured = true;
        }
        reset();
    }
    json accounts() {
        auto raw = request("/portfolio/accounts");
        require(raw.is_array() && raw.size() <= 100, "Invalid account list");
        account_cache = json::object();
        json rows = json::array();
        for (auto &r : raw) {
            auto a = r.value("accountId", r.value("id", std::string()));
            require(std::regex_match(a, std::regex("[A-Za-z0-9_-]{1,40}")),
                    "Invalid broker account ID");
            json row = {{"id", a},
                        {"currency", r.value("currency", json(nullptr))},
                        {"tradingType", r.value("tradingType", json(nullptr))}};
            account_cache[a] = row;
            rows.push_back(row);
        }
        return rows;
    }
    json margin(const std::string &a) {
        require(account_cache.contains(a),
                "Load this device's account list before querying margin");
        auto now = std::chrono::steady_clock::now();
        require(!margin_times.contains(a) || now - margin_times[a] > std::chrono::seconds(10),
                "Margin refresh interval is ten seconds");
        margin_times[a] = now;
        auto raw = request("/portfolio/" + a + "/summary");
        auto result = broker_margin(raw, a, epoch_ms());
        result["account_info"] = account_cache[a];
        return result;
    }
    json settings() const {
        return {{"device", device.substr(0, 12)},
                {"configured", configured},
                {"environment", mode},
                {"login_url",
                 mode == "user"
                     ? json("https://localhost:" + std::to_string(config["port"].get<int>()) + "/")
                     : json(nullptr)},
                {"scope", mode == "test" ? "test-device-only" : "this-device-only"},
                {"port", config["port"]},
                {"base_url",
                 "https://localhost:" + std::to_string(config["port"].get<int>()) + "/v1/api"},
                {"custom_ca", !config["ca_pem"].get<std::string>().empty()},
                {"authentication", "Client Portal Gateway browser login"}};
    }
    void save(const json &v) {
        auto next = ibkr_settings(v);
        gateway_environment(mode, next["port"].get<int>());
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        auto temp = file + ".tmp";
        {
            std::ofstream f(temp);
            require(bool(f), "Cannot save Gateway settings");
            auto stored = next;
            stored["device_id"] = device;
            f << stored.dump();
            f.flush();
            require(bool(f), "Cannot write Gateway settings");
        }
        require(chmod(temp.c_str(), 0600) == 0, "Cannot protect settings");
        std::filesystem::rename(temp, file);
        config = next;
        configured = true;
        prepared = false;
        reset();
    }
    json request(const std::string &path, bool post = false) {
        require(configured,
                "IBKR is not configured on this device; configure a local Gateway and log in here");
        gateway_environment(mode, config["port"].get<int>());
        if (path.starts_with("/portfolio/") && path.ends_with("/summary")) {
            auto account = path.substr(11, path.size() - 11 - 8);
            require(account_cache.contains(account),
                    "Margin endpoint requires discovered local account");
        }
        // This adapter is deliberately not a general-purpose broker proxy.
        require(
            (post && (path == "/iserver/auth/status" || path == "/iserver/auth/ssodh/init" ||
                      path == "/logout")) ||
                (!post &&
                 (path == "/iserver/accounts" || path == "/portfolio/accounts" ||
                  path == "/sso/validate" ||
                  std::regex_match(path, std::regex("/portfolio/[A-Za-z0-9_-]{1,40}/summary")) ||
                  path.starts_with("/iserver/secdef/search?symbol=") ||
                  std::regex_match(path, std::regex("/iserver/contract/[1-9][0-9]{0,11}/info")) ||
                  path.starts_with("/iserver/marketdata/snapshot?conids=") ||
                  path.starts_with("/iserver/marketdata/history?conid="))),
            "IBKR endpoint prohibited");
        require(std::chrono::steady_clock::now() - last > std::chrono::milliseconds(250),
                "IBKR pacing: wait briefly before retrying");
        last = std::chrono::steady_clock::now();
        std::string url = "https://localhost:" + std::to_string(config["port"].get<int>()) +
                          "/v1/api" + path,
                    body;
        auto c = handle.get();
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_PROXY, "");
        auto resolveText = "localhost:" + std::to_string(config["port"].get<int>()) + ":127.0.0.1";
        auto resolve = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>(
            curl_slist_append(nullptr, resolveText.c_str()), curl_slist_free_all);
        require(bool(resolve), "Cannot pin loopback address");
        curl_easy_setopt(c, CURLOPT_RESOLVE, resolve.get());
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        std::string pem = config["ca_pem"];
        curl_blob ca = {pem.data(), pem.size(), CURL_BLOB_COPY};
        curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, pem.empty() ? nullptr : &ca);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "ReachTerminal/IBKR-read-only");
        curl_easy_setopt(c, CURLOPT_POST, post ? 1L : 0L);
        if (post) {
            const char *payload =
                path == "/iserver/auth/ssodh/init" ? "{\"publish\":true,\"compete\":false}" : "";
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload);
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(payload)));
        } else
            curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(
            c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *u) -> size_t {
                auto &s = *static_cast<std::string *>(u);
                if (b && a > (2 * 1024 * 1024 - s.size()) / b)
                    return 0;
                s.append(p, a * b);
                return a * b;
            });
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
        auto headers = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>(
            path == "/iserver/auth/ssodh/init"
                ? curl_slist_append(nullptr, "Content-Type: application/json")
                : nullptr,
            curl_slist_free_all);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers.get());
        auto code = curl_easy_perform(c);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, nullptr);
        curl_easy_setopt(c, CURLOPT_RESOLVE, nullptr);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        require(code != CURLE_PEER_FAILED_VERIFICATION,
                "Gateway certificate is not trusted; configure its public CA certificate with "
                "localhost SAN");
        require(code == CURLE_OK,
                "Gateway unavailable; start Client Portal Gateway and check port/certificate");
        if (status == 401 || status == 403) {
            prepared = false;
            account_cache = json::object();
            margin_times.clear();
        }
        require(status >= 200 && status < 300, ibkr_http_error(path, status, body));
        auto raw = json::parse(body);
        require(!raw.is_object() || !raw.contains("error"),
                "IBKR returned an error or data entitlement is missing");
        return raw;
    }
    bool bootstrap_market() {
        market_accounts_ready = false;
        last_bootstrap = std::chrono::steady_clock::now();
        auto raw = request("/iserver/accounts");
        market_accounts_ready = raw.is_object() && raw.contains("accounts") &&
                                raw["accounts"].is_array() && !raw["accounts"].empty();
        return market_accounts_ready;
    }
    void prepare() {
        prepared = false;
        require(bootstrap_market(),
                "行情尚未就绪：网关没有返回可用的行情账户；请等待认证完成后重试");
        prepared = true;
    }
    json snapshot(const std::string &conid) {
        ibkr_conid(conid);
        require(prepared, "Click prepare market data first");
        return ibkr_snapshot(request("/iserver/marketdata/snapshot?conids=" + conid +
                                     "&fields=31,84,86,82,83,6509,7293,7294,7282,7087,7671,7672"),
                             conid);
    }
    json history(const std::string &conid) {
        ibkr_conid(conid);
        auto raw = request("/iserver/marketdata/history?conid=" + conid +
                           "&period=1m&bar=1d&outsideRth=false");
        require(raw.is_object() && raw.contains("data") && raw["data"].is_array() &&
                    raw["data"].size() <= 1000,
                "Invalid historical response");
        require(raw.value("barLength", 0) == 86400, "IBKR did not return daily bars");
        json out = {{"conid", conid},
                    {"symbol", raw.value("symbol", std::string())},
                    {"bar_seconds", 86400},
                    {"bars", json::array()},
                    {"availability", raw.value("mdAvailability", std::string("unknown"))}};
        for (auto &v : raw["data"]) {
            require(v.is_object() && v.contains("t") && v["t"].is_number_integer(),
                    "Invalid historical timestamp");
            json row = {{"timestamp", v["t"]}};
            for (auto k : {"o", "h", "l", "c", "v"}) {
                require(v.contains(k) && v[k].is_number(), "Missing history value");
                row[k] = v[k];
            }
            require(row["l"].get<double>() <=
                            std::min(row["o"].get<double>(), row["c"].get<double>()) &&
                        row["h"].get<double>() >=
                            std::max(row["o"].get<double>(), row["c"].get<double>()) &&
                        row["l"].get<double>() > 0 && row["v"].get<double>() >= 0,
                    "Invalid historical OHLCV");
            out["bars"].push_back(row);
        }
        return out;
    }
    json logout() {
        try {
            auto raw = request("/logout", true);
            require(raw.is_object() && raw.value("status", false),
                    "Gateway did not confirm logout; remote session state unknown");
        } catch (...) {
            prepared = false;
            reset();
            throw;
        }
        prepared = false;
        reset();
        return {{"logged_out", true}};
    }
    json status() {
        auto raw = request("/iserver/auth/status", true);
        if (!raw.value("authenticated", false))
            prepared = false;
        return {{"authenticated", raw.value("authenticated", false)},
                {"connected", raw.value("connected", false)},
                {"competing", raw.value("competing", false)}};
    }
    json connect_market() {
        prepared = false;
        auto raw = request("/iserver/auth/ssodh/init", true);
        market_init_until = std::chrono::steady_clock::now() + std::chrono::minutes(3);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        // Gateway completes the brokerage handshake through the account bootstrap.
        try {
            bootstrap_market();
        } catch (const std::exception &) {
            market_accounts_ready = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        require(raw.is_object(), "Invalid market session response");
        return {{"requested", true},
                {"authenticated", raw.value("authenticated", false)},
                {"competing", raw.value("competing", false)}};
    }
    json connection() {
        auto out = status();
        auto now = std::chrono::steady_clock::now();
        bool initiating = now < market_init_until;
        if (initiating && !out["competing"].get<bool>() &&
            (!out["authenticated"].get<bool>() || !market_accounts_ready) &&
            now - last_bootstrap >= std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            try {
                bootstrap_market();
            } catch (const std::exception &) {
                market_accounts_ready = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            out = status();
        }
        out["market_ready"] = out["authenticated"].get<bool>() && out["connected"].get<bool>() &&
                              (!initiating || market_accounts_ready);
        out["initializing"] =
            initiating && !out["market_ready"].get<bool>() && !out["competing"].get<bool>();
        if (now - last_sso >= std::chrono::seconds(60)) {
            last_sso = now;
            sso_checked = epoch_ms();
            sso_valid = nullptr;
            sso_error.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            try {
                auto raw = request("/sso/validate");
                require(raw.is_object() && raw.contains("RESULT") && raw["RESULT"].is_boolean(),
                        "Invalid SSO validation response");
                sso_valid = raw["RESULT"];
                if (sso_valid == false)
                    account_cache = json::object();
            } catch (const std::exception &e) {
                sso_error = e.what();
            }
        }
        out["sso_valid"] = sso_valid;
        out["sso_error"] = sso_error;
        out["sso_checked_at_ms"] = sso_checked ? json(sso_checked) : json(nullptr);
        out["checked_at_ms"] = epoch_ms();
        out["sso_min_interval_seconds"] = 60;
        return out;
    }
    json search(const std::string &q, bool name) {
        require(!q.empty() && q.size() <= 80, "Search needs 1-80 characters");
        auto encoded = curl_easy_escape(handle.get(), q.c_str(), static_cast<int>(q.size()));
        require(encoded, "Cannot encode query");
        std::string query(encoded);
        curl_free(encoded);
        return ibkr_search_rows(request("/iserver/secdef/search?symbol=" + query +
                                        "&name=" + (name ? "true" : "false")));
    }
    json detail(const std::string &conid) {
        ibkr_conid(conid);
        auto raw = request("/iserver/contract/" + conid + "/info");
        require(raw.is_object(), "Invalid contract details");
        auto actual = raw.contains("conid") ? raw["conid"] : raw.value("con_id", json(nullptr));
        require(ibkr_conid(actual) == conid, "IBKR contract identity mismatch");
        json out = {{"conid", conid}};
        for (auto k : {"symbol", "ticker", "companyName", "company_name", "secType",
                       "instrument_type", "currency", "exchange", "listingExchange", "industry",
                       "category", "validExchanges", "valid_exchanges"})
            if (raw.contains(k) && (raw[k].is_string() || raw[k].is_null()))
                out[k] = raw[k];
        return out;
    }
};
} // namespace reach
