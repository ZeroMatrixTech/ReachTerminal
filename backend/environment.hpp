#pragma once
#include "core.hpp"
namespace reach {
inline void workspace_environment(const std::string &mode, const std::string &database) {
    require(mode == "user" || mode == "test" || mode == "demo", "Invalid REACH_ENV");
    require(mode != "demo" || database == "reachterminal" ||
                database.starts_with("reachterminal_demo_"),
            "Demo runtime requires a demo database");
    require(mode != "user" || (database != "reachterminal" && !database.starts_with("reachterminal_demo_")),
            "User runtime cannot use the default demo database");
    require((mode == "test") == database.starts_with("reachterminal_test_"),
            "Test runtime requires a dedicated reachterminal_test_* database; user/demo cannot use "
            "test databases");
}
inline void gateway_environment(const std::string &mode, int port) {
    require(mode == "user" || mode == "test", "Gateway disabled in demo workspace");
    require(mode == "test" ? port == 5001 : port != 5001,
            "Mock Gateway is reserved on localhost:5001; real and test Gateways cannot be mixed");
}
inline void user_batch(const std::string &mode, const json &batch) {
    if (mode != "user")
        return;
    for (auto it = batch.begin(); it != batch.end(); ++it)
        if (it.value().is_array())
            for (auto &r : it.value())
                require(r.value("source", std::string()) != "Synthetic demo" &&
                            !r.value("id", std::string()).starts_with("demo:"),
                        "Synthetic demo data prohibited in user workspace");
}
} // namespace reach
