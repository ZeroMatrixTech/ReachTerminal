# 原生桌面终端

ReachTerminal 提供 C++20 / GTK 3 原生桌面前端，使用 Cairo 绘制图表，不包含 WebView，不需要浏览器或 Node.js 运行前端。当前支持 Linux 图形桌面。

桌面进程通过仅限 127.0.0.1 的 HTTP 接口连接 C++ 服务，数据库仍由 C++ 服务管理。耗时请求在独立工作线程执行；离开页面会丢弃过期读请求，但已经提交的保存、导入和导出继续完成。

## 启动

安装 GTK 3 开发库（Debian/Ubuntu：`libgtk-3-dev`），按主 README 编译，然后运行 `./scripts/desktop.sh`。脚本复用已经运行的用户服务；否则启动本机正式数据库服务，关闭窗口时只停止由该脚本启动的服务。脚本不会打开网页。

九个工作区包括组合核算、基本面研究、DCF 估值、风险与策略、新闻与笔记、数据中心、衍生品对冲、保证金和宏观数据。表格、文件选择、设置、表单和图表均为原生控件。衍生品使用模型估值，不能代替交易所报价；本终端没有下单接口。

IBKR 的个人 Client Portal Gateway 仍要求在官方外部登录页完成登录和双重验证；桌面数据中心中的登录入口只在点击时打开该官方页面。终端本身不依赖浏览器。登录后桌面监控认证和行情会话，并提供明确状态、手动连接及登出入口。网关地址、证书和会话始终绑定本机，不能跨设备迁移。

## 打包

运行 `python3 scripts/package-native.py --output /tmp/reachterminal-native-release`，生成带校验值的 Linux 动态链接程序包。包含原生前端、C++ 服务和启动脚本，不包含网关、设备配置、数据库、用户导出、账户或密钥。目标设备必须安装兼容 GTK 3、libpq、libcurl、OpenSSL、libxml2、cpp-httplib 等系统库，并准备自己的 PostgreSQL 数据库；这不是跨发行版静态包。程序包启动需要设置 `REACH_DATABASE_URL`，可用 `REACH_ROOT` 指定独立用户工作区。

## 验证与隔离

`ctest --test-dir build --output-on-failure` 验证原生数值适配与现有模型。`python3 scripts/test-isolated.py` 使用临时测试数据库、合成账户和模拟网关；原生自测遍历九个工作区并核验对冲结果，产生仅含合成数据的原生窗口预览。无图形桌面的 CI 使用 Xvfb。正式前端拒绝连接测试环境，自测前端拒绝连接用户环境。
