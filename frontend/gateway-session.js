export function sessionMessage(r) {
  if (r.competing)
    return {
      kind: "warning",
      text: "检测到竞争行情会话。请在 IBKR 核对其他设备或 TWS 登录；本终端不会自动抢占。",
    };
  if (
    r.initializing &&
    r.sso_valid === true &&
    (r.market_ready === false || !r.authenticated)
  )
    return {
      kind: "initializing",
      text: "账户登录有效，行情会话正在初始化。请等待认证完成后，再准备行情或查询标的；初始化请求返回不代表行情已可用。",
    };
  if (r.authenticated)
    return {
      kind: "success",
      text: "登录成功：行情会话已认证。可以搜索标的并准备行情。",
    };
  if (r.sso_valid === true)
    return {
      kind: "partial",
      text: "账户登录验证成功；行情会话尚未认证。账户与保证金可单独尝试读取，行情查询仍需 IBKR 完成行情认证。",
    };
  if (r.sso_valid === false)
    return {
      kind: "pending",
      text: "尚未确认登录成功。请在网关完成登录和双重验证，然后返回终端。",
    };
  return {
    kind: "error",
    text:
      "登录状态无法确认：" + (r.sso_error || "网关未返回有效会话验证结果。"),
  };
}
export function createGatewayMonitor({
  post,
  display,
  isCurrent,
  now = () => Date.now(),
  schedule = setTimeout,
  cancel = clearTimeout,
  visible = () => document.visibilityState !== "hidden",
}) {
  let revision = 0,
    timer = null,
    inFlight = false,
    watching = false,
    deadline = 0,
    requested = false,
    flightRevision = -1,
    lastChecked = 0,
    lastMessage = null;
  const commit = (m) => {
    lastMessage = m;
    display(m);
  };
  const clear = () => {
    if (timer !== null) cancel(timer);
    timer = null;
  };
  function stop() {
    revision++;
    watching = false;
    requested = false;
    lastMessage = null;
    clear();
  }
  async function check() {
    if (!isCurrent()) return;
    if (inFlight) {
      requested = revision !== flightRevision;
      return;
    }
    if (watching && now() >= deadline) {
      watching = false;
      commit({
        kind: "warning",
        text: "等待登录确认已超时，不能据此判断登录失败。可点击“检测登录状态”重新核对。",
      });
      return;
    }
    if (!visible()) return;
    const rev = revision;
    flightRevision = rev;
    inFlight = true;
    display(
      lastMessage
        ? { ...lastMessage, text: lastMessage.text + "（正在复查…）" }
        : { kind: "checking", text: "正在检查本机网关登录与行情认证…" },
    );
    try {
      const r = await post("/api/ibkr/connection", {});
      if (rev !== revision || !isCurrent()) return;
      if (watching && now() >= deadline) {
        watching = false;
        revision++;
        commit({
          kind: "warning",
          text: "等待登录确认已超时，不能据此判断登录失败。可点击“检测登录状态”重新核对。",
        });
        return;
      }
      lastChecked = now();
      const m = sessionMessage(r);
      lastMessage = {
        ...m,
        market_authenticated:
          r.authenticated === true && r.market_ready !== false,
        checked_at: r.checked_at_ms,
        sso_checked_at: r.sso_checked_at_ms,
      };
      display(lastMessage);
      if (r.authenticated || r.competing) watching = false;
    } catch (e) {
      if (rev === revision && isCurrent())
        commit({
          kind: "error",
          text: "检测失败：" + e.message + "。完成登录后可再次检测。",
        });
    } finally {
      inFlight = false;
      if (requested && isCurrent()) {
        requested = false;
        clear();
        timer = schedule(() => {
          timer = null;
          check();
        }, 0);
      } else if (watching && isCurrent()) {
        clear();
        timer = schedule(() => {
          timer = null;
          check();
        }, 5000);
      }
    }
  }
  function start(
    message = "已打开网关登录页，等待你完成登录与双重验证；返回此页将自动检测。",
  ) {
    stop();
    watching = true;
    deadline = now() + 180000;
    commit({
      kind: "pending",
      text: message,
    });
    check();
  }
  function focus() {
    if (
      isCurrent() &&
      !inFlight &&
      (!lastChecked || now() - lastChecked >= 5000)
    ) {
      clear();
      check();
    }
  }
  return { start, stop, check, focus };
}
