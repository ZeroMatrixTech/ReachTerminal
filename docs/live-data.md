# 真实公开数据接入（2026-10-15）

## 启动

在仓库目录运行 `./scripts/live.sh`，浏览器访问 http://127.0.0.1:8788 。
此脚本使用独立数据库 `reachterminal_live`，保留默认的合成演示数据库。
本机已经注册 IBM（`us:ibm`、CIK 51143），并通过 C++ 接口抓取 SEC CompanyFacts 与日线。

在数据中心创建其他证券，填写真实的 SEC CIK，再点击 SEC 抓取。日线当前仅接受 US/USD 股票。其他市场继续使用已有标准格式导入。

## 本机配置

`.local/live.env` 不进入版本控制，权限建议 600：

```sh
REACH_SEC_USER_AGENT='ReachTerminal your-contact@example.org'
# 可选，个人免费密钥；不配置时仅可使用 IBM 的官方公开试用。
REACH_ALPHA_API_KEY='YOUR_PERSONAL_KEY'
```

修改配置后重启服务。密钥只在后端使用；接口状态、来源 URL、原始文档不记录请求密钥。服务不会自动注册账户或购买订阅。

## 来源与口径

- [SEC 官方 API](https://www.sec.gov/search-filings/edgar-application-programming-interfaces)：无 API 密钥，需要真实联系标识。保持披露日期过滤、CIK 绑定、原始文档和修订版本。US-GAAP/DEI 映射不完整，旧标签会显示旧报告期和过期提示；不将缺失值补零。
- [Alpha Vantage 文档](https://www.alphavantage.co/documentation/)：`TIME_SERIES_DAILY` 的 compact 返回最近 100 个交易日原始 OHLCV。公开 demo 仅用于 IBM 链路试用，可能有缓存和访问限制；更多证券需要个人免费密钥与相应权限，免费额度由供应商决定。
- 日线不是实时行情。保守排除 UTC 当天可能未完成的交易日，不做拆股或分红复权；不能将复权价用于原始持仓账本估值。
- 网络、额度、缺字段、证券不符、非法 OHLC 与未来日期都会明确失败，不生成替代数据。用户手动触发，单进程至少间隔 15 秒；未实现跨进程每日配额计数或自动重试。
- 数据仍遵守不可变导入契约：同日期被供应商修订会拒绝整个批次，不偷偷覆盖。修订工作流尚待实现。
- 第三方数据不因代码 MIT 而获得再分发许可；真实响应仅保留本机数据库，不作为开源示例提交。

## 验收

2026-10-15 实际经 C++ HTTPS → 标准化 → PostgreSQL → 研究页面完整链路验证：
IBM 1,744 条财务版本、100 根日线，最新日期 2026-10-14，原始收盘价 227.88 USD。
真实工作区无合成记录、虚构成交或现金流水。

核心回归 66 项断言通过；独立临时数据库的原有 HTTP 集成检查 42 项通过。
浏览器核对研究页财务披露日期、日线、来源和过期提示；可重复运行只读检查：

```sh
python3 scripts/check-live-data.py --url http://127.0.0.1:8788
```

没有连接真实 IBKR 账户，没有声称免费全球市场全覆盖。当前接口在单进程数据库锁内执行网络请求，抓取期间其他读取可能等待（单次超时 45 秒）；任务队列和增量缓存属后续优化。

`check-live-data.py` 是用户手动发起的只读诊断，不参与自动化测试。自动化测试统一使用 `scripts/test-isolated.py`；tests/ 下的接口脚本均拒绝用户实例。
