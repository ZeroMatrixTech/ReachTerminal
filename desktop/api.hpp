#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <stdexcept>
#include <thread>
using json = nlohmann::json;
class LocalApi {
    std::string base, token, expected;
    std::atomic<bool> &stopping;
    std::chrono::steady_clock::time_point last{};
    json transport(const std::string &path, const std::string &body, bool post,
                   const std::string &type) {
        if (!path.starts_with("/api/") || path.find('\r') != std::string::npos ||
            path.find('\n') != std::string::npos)
            throw std::runtime_error("Invalid local API path");
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(300))
            std::this_thread::sleep_for(std::chrono::milliseconds(300) - (now - last));
        last = std::chrono::steady_clock::now();
        auto c = curl_easy_init();
        if (!c)
            throw std::runtime_error("Cannot initialize local connection");
        std::string data, url = base + path;
        auto headers = curl_slist_append(nullptr, ("X-Reach-Token: " + token).c_str());
        headers = curl_slist_append(headers, ("Content-Type: " + type).c_str());
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_PROXY, "");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 2L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        if (post) {
            curl_easy_setopt(c, CURLOPT_POST, 1L);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }
        curl_easy_setopt(
            c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *u) -> size_t {
                auto &s = *static_cast<std::string *>(u);
                if (b && a > (32 * 1024 * 1024 - s.size()) / b)
                    return 0;
                s.append(p, a * b);
                return a * b;
            });
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(
            c, CURLOPT_XFERINFOFUNCTION,
            +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return static_cast<std::atomic<bool> *>(p)->load() ? 1 : 0;
            });
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &stopping);
        auto code = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(c);
        if (code != CURLE_OK)
            throw std::runtime_error("本地后端连接失败：" + std::string(curl_easy_strerror(code)) +
                                     "。请检查终端后端是否正在运行。");
        json v;
        try {
            v = json::parse(data);
        } catch (...) {
            throw std::runtime_error("本地后端返回无效数据");
        }
        if (status < 200 || status >= 300)
            throw std::runtime_error(
                v.value("error", std::string("本地请求失败 HTTP ") + std::to_string(status)));
        return v;
    }

  public:
    LocalApi(int port, const std::string &mode, std::atomic<bool> &stop)
        : base("http://127.0.0.1:" + std::to_string(port)), expected(mode), stopping(stop) {
        if (port < 1 || port > 65535)
            throw std::runtime_error("Invalid local port");
    }
    void connect() {
        auto s = transport("/api/session", "", false, "application/json");
        if (s.value("environment", std::string()) != expected)
            throw std::runtime_error("工作区环境不匹配，已拒绝连接以保护用户数据");
        token = s.at("token");
    }
    json get(const std::string &p) {
        if (token.empty())
            connect();
        return transport(p, "", false, "application/json");
    }
    json post(const std::string &p, const json &v = json::object()) {
        if (token.empty())
            connect();
        return transport(p, v.dump(), true, "application/json");
    }
    json raw(const std::string &p, const std::string &v, const std::string &type) {
        if (token.empty())
            connect();
        return transport(p, v, true, type);
    }
    static std::string encode(const std::string &s) {
        std::string out;
        const char *h = "0123456789ABCDEF";
        for (unsigned char c : s) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.')
                out += static_cast<char>(c);
            else {
                out += '%';
                out += h[c >> 4];
                out += h[c & 15];
            }
        }
        return out;
    }
};
