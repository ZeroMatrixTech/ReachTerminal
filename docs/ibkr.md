# IBKR Client Portal Gateway 接入

个人账户使用 Client Portal Gateway 浏览器登录与双重验证，不使用通用 API Key。

## 当前机器

官方网关已解包至 `.local/clientportal/`，本地 Java 至 `.local/java-runtime-21/`；均被 Git 忽略。
默认端口 5000，独立 localhost 证书的公钥信任已配置到 `.local/devices/<device>/ibkr.json`，文件权限 600。
不修改系统 Java 或系统证书信任。不要提交 `.local/` 或网关日志。

```sh
./scripts/gateway.sh  # 独立终端窗口，保持运行
./scripts/live.sh     # 独立终端窗口，http://127.0.0.1:8788
```

本人在 https://localhost:5000/ 登录并完成双重验证。浏览器的本机证书提示由本人核对处理。
在 ReachTerminal 的数据中心：

1. 检测登录状态；未认证时回到官方网关登录。
2. 按证券代码或公司名搜索，核对市场和 conid，选择合约。
3. 点击“准备行情”，满足 IBKR 的账户初始化前置要求（账户列表不会返回到前端）。
4. 获取行情与研究指标。首次请求可能只是预热，稍后手动刷新；显示实时/延迟/冻结/未订阅状态及更新时间。
5. 获取近月日线预览。保留供应商时间戳、成交量及可用性代码。

## 在其他机器安装

从 [IBKR 官方链接](https://download2.interactivebrokers.com/portal/clientportal.gw.zip) 下载并解包到 `.local/clientportal/`，安装 Java（官方包说明最低 8u192，建议现代受支持版本）。也可用 `REACH_GATEWAY_DIR` 指定已安装目录。
按官方网关说明配置自己的 HTTPS 证书。数据中心填写本机端口以及可信证书 PEM；证书应覆盖 localhost。留空表示系统信任，保存空值会清除已有自定义证书。
本仓库不分发官方网关、Java、私钥或第三方数据。

## 已实现范围

- 网关连接设置保存/重新加载、登录与竞争会话状态。
- 证券代码/公司名搜索，conid 绑定的合约详情（兼容 IBKR 文档中 camelCase 和 snake_case 公共字段）。
- 买卖价、最新价、价格变化、52 周区间、平均成交量、历史波动率、股息字段；保留原始单位及 C/H 前缀，缺失值不会补零。
- 一个月、1 日 bar 的历史数据预览。

这些字段不是完整公司财报或宏观经济数据库。完整 US-GAAP 财报使用已有 SEC 接口；宏观指标、经济日历、其他基本面供应商尚未实现。数据覆盖取决于账户与订阅权限，不自动购买行情。

## 数据与安全边界

连接固定 localhost 并固定解析到 127.0.0.1，禁用代理和重定向，严格验证 TLS。没有“忽略证书”开关。只允许明确列出的查询路径及认证状态 POST；不暴露任意代理、下单或收费 regulatory snapshot 接口。

凭据由官方网关管理。连接表单拒绝 API Key、账户密码和私钥；会话 Cookie 只保存在后端内存。前端状态不返回 MAC/硬件详情或账户列表。

快照与日线是手动查询预览，不写入账本和现有日终报价。日线交易日时区、volumeFactor 和复权尚未标准化，因此不用于账户估值。最小请求间隔 250ms，单次网络超时 15s，无自动登录、自动接管会话或无限重试。网络请求仍占用现有单实例串行处理锁。

## 验证与当前限制

核心断言 81 项、原有 HTTP 回归 42 项通过；隔离的 HTTPS 模拟网关 21 项检查通过，覆盖证书、认证、编码、权限缺失和账本不变性。
2026-10-16，用户本人完成官方网关登录后，已验证真实认证、IBM 跨市场合约搜索、纽约合约详情、延迟快照（含股息、波动率字段）及 21 根近月日线。浏览器已验收搜索、选定合约、快照与日线入口。真实搜索发现 conid=-1 的债券发行人分组，已省略这类非具体合约分组并新增回归断言。模拟结果只用于测试，不能作为市场数据。

官方接口参考：[认证状态](https://www.interactivebrokers.com/docs/web-api/v1/endpoints/session/authentication-status)、[合约搜索](https://www.interactivebrokers.com/docs/web-api/v1/endpoints/contract/search-contract-by-symbol)、[行情字段](https://www.interactivebrokers.com/docs/web-api/v1/endpoints/market-data/market-data-fields)、[历史行情](https://www.interactivebrokers.com/docs/web-api/v1/endpoints/market-data/historical-market-data)。

隔离 HTTPS 回归脚本为 `tests/ibkr_mock.py` 和 `tests/ibkr_gateway_test.py`。必须使用空的临时数据库、独立 REACH_ROOT 配置目录，避免覆盖正常网关设置。准备 fixture 目录并用 openssl 生成覆盖 localhost 的 mock-cert.pem / mock-key.pem，启动模拟网关 `python3 tests/ibkr_mock.py --fixture-dir DIR`（端口5001），再对临时终端运行 `python3 tests/ibkr_gateway_test.py --url http://127.0.0.1:8790 --fixture-dir DIR`。脚本会保存测试证书配置，不能对正常工作区运行。

## 不同设备隔离

网关不可迁移。每台设备独立安装、生成本机证书并本人登录；禁止同步或搬运整个 `.local/`、网关目录、会话 Cookie、证书私钥及 Java 信任配置。Git 仅共享代码，工作区导出仅包含研究数据，不包含网关状态。

后端根据 Linux `/etc/machine-id` 的带用途前缀哈希使用 `.local/devices/<device>/ibkr.json`；配置还校验所属设备。旧的无归属配置不会自动加载。本机网关目录有 `.reach-device.json`，启动脚本拒绝其他设备或没有归属标记的目录。当前机器进行了明确的一次性配置归属，其他设备必须重新安装和配置。

机器身份依赖操作系统 machine-id；克隆系统镜像必须确保各机器有独立 machine-id。这是设备归属检查，不是硬件防篡改证明。新设备禁止直接给搬运来的旧目录重新打标。

在新设备安装时先从上述官方链接下载全新 ZIP，再运行 `python3 scripts/provision_gateway.py --archive /path/clientportal.gw.zip --destination .local/clientportal`。目标目录必须不存在；包含设备归属标记的搬运归档会被拒绝，不能给旧目录重新绑定。随后配置独立的本机证书及 Java，保存本设备连接设置并本人登录。

## 登录、登出和测试隔离

数据中心的“打开网关登录”会在新标签页打开本设备配置端口的官方网关页面。网关进程需已启动；账户登录及双重验证仍由本人完成。

“登出网关”调用 `POST /logout`，仅在 IBKR 返回 `status: true` 时显示成功。登出成功或失败都会清除本地行情准备状态及前端查询缓存；登出期间阻止新查询，较早的未完成查询不能重新填回缓存。网络错误时明确提示远端登出未确认。连接配置和研究数据不会被删除。

运行环境分为 `user`、`demo`、`test`。`scripts/live.sh` 固定 user，默认 `scripts/dev.sh` 为 demo。实际连接的数据库名由 PostgreSQL/libpq 核对：test 必须为 `reachterminal_test_*`，user/demo 禁止使用测试库，user 与默认/命名演示库也互斥。测试与演示网关配置不读取 user 的设备配置；demo 禁止访问网关，test 仅允许 localhost:5001 模拟网关，user 禁止使用该端口。test/demo 不能执行外部 SEC/Alpha 抓取。user 禁止导入带合成演示标识的记录。

所有会写数据的测试统一运行 `python3 scripts/test-isolated.py`。启动器清除真实数据源环境变量，创建独立私有 PostgreSQL 集群、独立 socket/数据库、临时应用根目录、测试证书和模拟网关；结束后停止全部测试进程。测试报告留在忽略的 `.local/test-runs/`。测试脚本即使误填用户地址，也会在只读环境预检后拒绝，不会修改用户配置或登出真实会话。旧的手工测试步骤由此启动器替代。

本次验证：88 项核心断言、28 项网关 HTTPS 检查、42 项核算与导入回归通过。登出只在模拟网关验收，未对用户真实会话执行登出。测试前后用户工作区导出及设备连接配置 SHA-256 完全一致。浏览器已验证登录入口打开正确的本机官方登录页。
