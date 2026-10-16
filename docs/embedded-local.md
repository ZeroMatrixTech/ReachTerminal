# C++ 本地内嵌版与网关登录反馈

前端资源和数据库初始化 SQL 在构建时由明确白名单生成 C++ 资源，随 `reachterminal` 可执行程序发布。运行时不读取 `frontend/` 或 `db/`，资源修改后需要重新编译。浏览器仍是交互界面，PostgreSQL、操作系统动态库和本设备 IBKR Gateway 单独安装。

构建需要 Python 3，浏览器状态测试需要 Node.js；运行用户终端不需要两者。源码包包含资源生成器及原始可编辑前端。创建本地二进制包：

```sh
python3 scripts/package-binary.py --output .local/releases
```

二进制包仅含程序、启动脚本、许可证和说明。`start.sh` 要求显式配置本机 PostgreSQL 的 `REACH_DATABASE_URL`，状态目录默认为包内 `workspace/`，可通过 `REACH_ROOT` 修改。当前开发安装继续使用 `scripts/live.sh`，保留原本设备配置。包不会搬运数据库、网关或登录状态，也不会复制系统动态库。

## 登录流程

1. 数据中心首次打开自动检测登录状态。
2. 点击“打开网关登录”后，终端开始三分钟的等待确认；登录、双重验证由用户在官方页面完成。
3. 返回终端会自动复查，分别显示账户会话验证与行情认证。SSO 验证遵守官方每分钟一次的限额，界面显示该验证的时间。
4. 账户验证成功且行情未认证，显示部分连接成功，而非“登录失败”。用户可主动点击“连接行情会话（不抢占）”，使用固定 `publish:true, compete:false` 请求认证；不会自动抢占 TWS 或其他设备的行情会话。认证后再点击“准备行情”。
5. 显示具体连接错误、竞争会话或超时未确认。超时不等同于登录失败；手动检测可重新核对。

隐藏页面暂停轮询；导航、设置变更与登出中止当前监控。异步响应受代次检查，过期响应不会覆盖新页面或退出状态。返回原始 SSO 用户名、用户 ID、凭证、IP 等均被禁止，仅投影验证布尔值与检测时间。

官方依据：[两层会话](https://www.interactivebrokers.com/docs/web-api/authentication/sessions)、[SSO 验证](https://www.interactivebrokers.com/docs/web-api/api-reference/trading/trading-session/get-session-validation)、[会话初始化与两秒等待](https://www.interactivebrokers.com/docs/web-api/authentication/trading)、[API 速率限制](https://www.interactivebrokers.com/campus/ibkr-api-page/webapi-doc/)。

行情初始化的账户握手和 HTTP 400 故障说明见 [本次修复](ibkr-400-fix.md)。
