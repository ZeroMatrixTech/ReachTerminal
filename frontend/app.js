const $ = (s, root = document) => root.querySelector(s);
const $$ = (s, root = document) => [...root.querySelectorAll(s)];
const escape = (v) =>
  String(v ?? "").replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ],
  );
const fmt = (v, d = 2) =>
  v == null
    ? "—"
    : new Intl.NumberFormat("en-US", {
        minimumFractionDigits: d,
        maximumFractionDigits: d,
      }).format(v);
const compact = (v) =>
  v == null
    ? "—"
    : new Intl.NumberFormat("en-US", {
        notation: "compact",
        maximumFractionDigits: 2,
      }).format(v);
const money = (v) =>
  v == null ? "—" : `${v < 0 ? "−" : ""}$${fmt(Math.abs(v))}`;
const pct = (v) => (v == null ? "—" : `${fmt(v * 100, 1)}%`);
const sign = (v) => (v == null ? "" : v >= 0 ? "positive" : "negative");
const link = (u, label = "来源 ↗") =>
  /^https?:\/\//.test(u ?? "")
    ? `<a class="source-link" href="${escape(u)}" target="_blank" rel="noopener noreferrer">${escape(label)}</a>`
    : `<span class="text-muted">${escape(u || "未提供来源")}</span>`;
const names = {
  overview: "组合总览",
  research: "公司研究",
  valuation: "估值工作台",
  risk: "风险与策略",
  news: "新闻与研究",
  data: "数据中心",
};
let state = {
  page: "overview",
  instruments: [],
  selected: null,
  token: "",
  asof: new Date().toISOString().slice(0, 10),
  portfolio: null,
  research: null,
  data: null,
};
let generation = 0,
  toastTimer;
function toast(message, error = false) {
  $("#toast").textContent = message;
  $("#toast").className = `toast${error ? " error" : ""}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $("#toast").classList.add("hidden"), 7000);
}
async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "X-Reach-Token": state.token,
      ...(options.body && typeof options.body === "string" && !options.raw
        ? { "Content-Type": "application/json" }
        : {}),
      ...options.headers,
    },
  });
  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error("服务返回了无效响应");
  }
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}
const post = (path, data) =>
  api(path, { method: "POST", body: JSON.stringify(data) });
const qs = () => `as_of=${encodeURIComponent(state.asof)}`;
function heading(eyebrow, title, sub, actions = "") {
  return `<div class="page-heading"><div><div class="eyebrow">${eyebrow}</div><h1>${title}</h1><p class="sub">${sub}</p></div><div class="actions">${actions}</div></div>`;
}
function stat(label, value, sub = "", cls = "") {
  return `<div class="stat"><div class="stat-label">${label}</div><div class="stat-value ${cls}">${value}</div><small>${sub}</small></div>`;
}
function panel(title, body, meta = "", classes = "") {
  return `<section class="panel ${classes}"><div class="panel-head"><h2>${title}</h2><small>${meta}</small></div>${body}</section>`;
}
function table(headers, rows) {
  return `<div class="table-wrap"><table><thead><tr>${headers.map((h) => `<th>${h}</th>`).join("")}</tr></thead><tbody>${rows.join("") || `<tr><td colspan="${headers.length}" class="empty">暂无数据 · 从数据中心导入</td></tr>`}</tbody></table></div>`;
}
function chart(id, caption = "") {
  return `<div class="chart-wrap"><canvas id="${id}" class="chart" aria-label="${escape(caption.replace(/<[^>]*>/g, ""))}" role="img"></canvas><div id="${id}-tip" class="chart-tip"></div></div><div class="chart-caption">${caption}</div>`;
}
function universe() {
  $("#symbol-picker").innerHTML = state.instruments
    .map(
      (i) =>
        `<option value="${escape(i.id)}" ${i.id === state.selected ? "selected" : ""}>${escape(i.symbol)}</option>`,
    )
    .join("");
  let search = $("#search").value.toLowerCase();
  $("#universe-count").textContent = state.instruments.length;
  $("#universe").innerHTML = state.instruments
    .filter((i) =>
      `${i.symbol} ${i.name} ${i.exchange}`.toLowerCase().includes(search),
    )
    .map(
      (i) =>
        `<button class="symbol ${i.id === state.selected ? "active" : ""}" data-instrument="${escape(i.id)}"><span><strong>${escape(i.symbol)}</strong><small>${escape(i.name)}</small></span><span class="region">${escape(i.exchange)}</span></button>`,
    )
    .join("");
}
function positionsTable(p) {
  return table(
    [
      "标的 / 市场",
      "策略",
      "数量",
      "价格 / 日期",
      "市值 · USD",
      "未实现盈亏 · USD",
    ],
    p.positions
      .filter((r) => r.quantity !== 0)
      .map(
        (r) =>
          `<tr class="clickable" data-instrument="${escape(r.instrument)}"><td><strong>${escape(r.symbol)}</strong><small>${escape(r.name)} · ${escape(r.currency)}</small></td><td><span class="pill">${escape(r.strategy)}</span></td><td class="num">${fmt(r.quantity, 4)}</td><td class="num">${fmt(r.price)}<small>${escape(r.quote_date || "缺少报价")} · ${escape(r.quote_status || "")}</small></td><td class="num">${money(r.market_value_base)}</td><td class="num ${sign(r.unrealized_base)}">${money(r.unrealized_base)}</td></tr>`,
      ),
  );
}
function exposures(values) {
  const total = Object.values(values).reduce((a, b) => a + b, 0);
  return `<div class="panel-body">${
    Object.entries(values)
      .sort((a, b) => b[1] - a[1])
      .map(
        ([k, v]) =>
          `<div class="exposure-row"><span>${escape(k)}</span><svg viewBox="0 0 100 4" aria-hidden="true"><rect width="100" height="4" fill="#213345"/><rect width="${total > 0 ? Math.max(0, (v / total) * 100) : 0}" height="4" fill="#49cbbb"/></svg><strong>${pct(total > 0 ? v / total : null)}</strong></div>`,
      )
      .join("") || '<p class="help">没有持仓数据</p>'
  }<p class="help">占比以股票总市值为分母，不含现金。</p></div>`;
}
function emptyWorkspace() {
  return panel(
    "开始你的研究工作区",
    `<div class="empty large-empty"><div class="eyebrow">YOUR EDGE STARTS WITH CLEAR DATA</div><h2>让数据、投资论点与组合风险对齐</h2><p>导入真实交易与基本面，建立自己的研究终端。也可以先加载三个虚构全球公司的演示数据，体验完整工作流。</p><button class="primary" id="load-demo">加载合成演示数据</button> <button data-page="data">导入我的数据</button><p class="help">演示只允许加载到空数据库，避免与真实账户混合。</p></div>`,
  );
}
async function render() {
  const g = ++generation;
  $("#page-name").textContent = names[state.page];
  $$("#nav button").forEach((b) =>
    b.classList.toggle("active", b.dataset.page === state.page),
  );
  $("#content").innerHTML = '<div class="loading">正在读取研究数据…</div>';
  try {
    if (state.page === "data") {
      const data = await api("/api/data");
      if (g !== generation) return;
      state.data = data;
      renderData(data);
      return;
    }
    if (!state.instruments.length) {
      $("#content").innerHTML =
        heading(
          "REACH / INVESTMENT WORKSPACE",
          "你的投资研究终端",
          "本地运行 · 开源 · 只读账户接入",
        ) + emptyWorkspace();
      return;
    }
    if (state.page === "overview" || state.page === "risk") {
      const p = await api(`/api/portfolio?${qs()}`);
      if (g !== generation) return;
      state.portfolio = p;
      state.page === "overview" ? renderOverview(p) : renderRisk(p);
    } else if (state.page === "research" || state.page === "valuation") {
      const r = await api(
        `/api/research?instrument=${encodeURIComponent(state.selected)}&${qs()}`,
      );
      if (g !== generation) return;
      state.research = r;
      state.page === "research"
        ? await renderResearch(r, g)
        : renderValuation(r);
    } else if (state.page === "news") {
      const [news, r] = await Promise.all([
        api(`/api/news?${qs()}`),
        api(
          `/api/research?instrument=${encodeURIComponent(state.selected)}&${qs()}`,
        ),
      ]);
      if (g !== generation) return;
      state.research = r;
      renderNews(news, r);
    }
  } catch (e) {
    if (g !== generation) return;
    $("#content").innerHTML = panel(
      "数据读取失败",
      `<div class="empty"><h2>暂时无法显示此工作区</h2><p>${escape(e.message)}</p><button id="retry">重试</button></div>`,
    );
    toast(e.message, true);
  }
}
function renderOverview(p) {
  $("#content").innerHTML =
    heading(
      "PORTFOLIO / GLOBAL VIEW",
      "组合总览",
      "从账户净资产到策略归因，保持同一套核算口径。",
      `<button data-page="data">导入与对账 ↗</button>`,
    ) +
    `<div class="grid stats">${stat("净资产 · USD", money(p.nav), "历史现金流水 + 当前持仓估值")}${stat("累计经济盈亏", money(p.economic_pnl), "已扣除净入金 · 包含汇率影响", sign(p.economic_pnl))}${stat("股票市值", money(p.market_value), `${p.positions.filter((v) => v.quantity).length} 个策略持仓`)}${stat("现金余额", money(p.cash_value), "多币种按分析日期汇率折算")}</div><div class="grid two-col">${panel("组合表现", chart("nav-chart", "指数按日末外部现金流近似调整；历史缺失会中断曲线。"), '<span class="legend"><i></i> RETURN INDEX · BASE 100</span>')}${panel("行业配置", exposures(p.sector_exposure), "EQUITY EXPOSURE")}</div>${panel("持仓明细", positionsTable(p), `${escape(p.as_of)} · 点击标的进入研究`)}<div class="grid equal-col section-gap">${panel(
      "现金与资金流",
      table(
        ["账户 / 币种", "余额", "折算 · USD"],
        p.cash.map(
          (c) =>
            `<tr><td>${escape(c.account)} <span class="pill">${escape(c.currency)}</span></td><td class="num">${fmt(c.amount)}</td><td class="num">${money(c.base_value)}</td></tr>`,
        ),
      ) +
        `<div class="metric-note">净外部资金流：${money(p.net_external_flows)} · 入金不计入投资收益。</div>`,
    )}${panel("数据质量与核算", `<div class="panel-body"><p class="${p.complete ? "positive" : "negative"}">${p.complete ? "● 必要估值数据齐全" : "● 估值不完整，汇总已隐藏"}</p>${p.warnings.length ? `<ul class="warning-list">${p.warnings.map((w) => `<li>${escape(w)}</li>`).join("")}</ul>` : '<p class="help">当前没有报价或汇率缺失提醒。</p>'}<p class="help">完整性指已导入数据的计算条件；请通过经纪商报表核验历史是否完整。</p><p class="help">近似收益指数最大回撤：${pct(p.max_drawdown)}</p></div>`)}</div>`;
  drawLine(
    "nav-chart",
    p.curve.map((x) => ({ date: x.date, value: x.return_index })),
    false,
  );
}
function renderRisk(p) {
  $("#content").innerHTML =
    heading(
      "RISK / STRATEGY ATTRIBUTION",
      "风险与策略",
      "先看敞口与损失情景，再看策略贡献。",
      `<span class="badge">CASH EQUITIES</span>`,
    ) +
    `<div class="grid stats">${stat("近似收益指数最大回撤", pct(p.max_drawdown), "仅基于已导入历史")}${stat("累计已实现盈亏", money(p.realized_base), "策略内 FIFO · 已包含成交费用", sign(p.realized_base))}${stat("累计未实现盈亏", money(p.unrealized_base), "历史成本汇率与当前估值汇率", sign(p.unrealized_base))}${stat("经纪商保证金", "—", "未接入 · 内部情景不代表经纪商要求")}</div>${panel(
      "策略归因",
      table(
        [
          "策略",
          "股票市值 · USD",
          "已实现",
          "未实现",
          "收入 / 费用",
          "策略盈亏",
        ],
        p.strategies.map(
          (s) =>
            `<tr><td><span class="pill">${escape(s.name)}</span></td><td class="num">${money(s.market_value_base)}</td><td class="num">${money(s.realized_base)}</td><td class="num">${money(s.unrealized_base)}</td><td class="num">${money(s.income_base)}</td><td class="num ${sign(s.pnl_base)}">${money(s.pnl_base)}</td></tr>`,
        ),
      ) +
        `<div class="metric-note">策略内独立 FIFO；现金汇率重估与换汇差额未分配到股票策略，策略盈亏之和可能与账户经济盈亏不同。</div>`,
    )}<div class="grid equal-col section-gap">${panel("组合压力测试", `<div class="panel-body"><form id="stress-form" class="form-grid"><label>股票价格冲击 · %<input name="equity_shock" type="number" value="-10" min="-100" max="100" step="1" required></label><label>非美元币种兑美元 · %<input name="fx_shock" type="number" value="-5" min="-100" max="100" step="1" required></label><div class="wide"><button class="primary" ${p.complete ? "" : "disabled"}>计算压力情景</button><p class="help">价格与汇率冲击联合重估，并包含外币现金。当前只支持现金股票；不计算期权非线性风险、强平阈值或保证金。</p></div></form><div id="stress-result"></div></div>`)}${panel("币种敞口", exposures(p.currency_exposure), "STOCK MARKET VALUE")}</div>${panel(
      "经纪商快照对账",
      table(
        ["日期", "账户", "检查项", "差异", "状态"],
        p.reconciliation.map((r) => {
          let n = r.cash_difference ?? r.quantity_difference;
          return `<tr><td>${escape(r.date)}</td><td>${escape(r.account)}</td><td>${r.cash_difference != null ? "现金 " + escape(r.currency) : "持仓 " + escape(r.instrument)}</td><td class="num">${fmt(n, 6)}</td><td class="${Math.abs(n) < 0.000001 ? "check-pass" : "check-fail"}">${Math.abs(n) < 0.000001 ? "一致" : "需检查历史 / 公司行动"}</td></tr>`;
        }),
      ) +
        `<div class="metric-note">仅显示与分析日期相同的快照。没有快照时，不代表已通过对账。</div>`,
    )}`;
}
async function renderResearch(r, g) {
  const i = r.instrument_info,
    m = r.metrics;
  $("#content").innerHTML =
    `<div class="detail-banner"><div class="company-avatar">${escape(i.symbol.slice(0, 2))}</div><div><div class="eyebrow">EQUITY / FUNDAMENTAL RESEARCH</div><h1>${escape(i.name)}</h1><div class="company-meta"><span>${escape(i.symbol)}</span><span>${escape(i.exchange)}</span><span>${escape(i.country)}</span><span>${escape(i.sector)}</span><span>${escape(i.currency)}</span></div></div></div><div class="grid stats">${stat("股价 · " + escape(i.currency), fmt(m.price), r.quote ? `${escape(r.quote.date)} · ${escape(r.quote.status)} · ${escape(r.quote.source)}` : "缺少报价")}${stat("年度市盈率", m.pe == null ? "—" : fmt(m.pe) + "×", "最新已披露年度稀释 EPS")}${stat("市净率", m.pb == null ? "—" : fmt(m.pb) + "×", "使用最新已披露股数与净资产")}${stat("经营现金流 − 资本开支收益率", pct(m.fcf_yield), "年度 OCF − CapEx / 市值")}</div><div class="grid two-col">${panel("价格图表", chart("price-chart", `${escape(r.bars[0]?.source || "未提供行情")} · 原始 OHLC，未自动复权；非实时。${r.quote ? " " + link(r.quote.source_url, "行情来源 ↗") : ""}`), '<div class="period-tabs"><button class="active" data-period="all">ALL</button><button data-period="90">3M</button><button data-period="30">1M</button></div>')}${panel("研究概览", `<div class="panel-body"><div class="exposure-row"><span>市值 · ${escape(i.currency)}</span><strong>${compact(m.market_cap)}</strong></div><div class="exposure-row"><span>年度净利率</span><strong>${pct(m.net_margin)}</strong></div><div class="exposure-row"><span>当前可用财务记录</span><strong>${r.history.length}</strong></div><p class="help">年度与时点财务数据；季度记录保留在历史中，缺失数据不会自动补齐。</p>${m.warnings.map((w) => `<p class="accent">${escape(w)}</p>`).join("")}<button data-page="valuation">进入估值工作台 →</button></div>`)}</div>${panel(
      "财务数据 · 原始披露可追溯",
      table(
        ["指标", "数值", "单位", "报告期间", "披露日期", "来源"],
        r.selected.map(
          (f) =>
            `<tr><td>${escape(concepts[f.concept] || f.concept)}<small>${escape(f.taxonomy)}:${escape(f.tag)}</small></td><td class="num">${compact(Number(f.value))}</td><td>${escape(f.unit)}</td><td>${escape(f.period_end)}<small>${escape(f.period_kind)}</small></td><td>${escape(f.filed)}</td><td>${link(f.source_url)}</td></tr>`,
        ),
      ) +
        `<div class="metric-note">选取截止 ${escape(state.asof)} 已披露版本，优先最新报告期。同期间保留重述历史；跨市场口径需独立核验。</div><details><summary>查看全部历史披露与版本（${r.history.length}）</summary><div class="facts-history">${table(
          ["指标", "期间", "披露", "值 / 单位", "申报编号"],
          r.history.map(
            (f) =>
              `<tr><td>${escape(f.concept)}</td><td>${escape(f.period_end)}</td><td>${escape(f.filed)}</td><td class="num">${compact(Number(f.value))} ${escape(f.unit)}</td><td>${escape(f.accession)}</td></tr>`,
          ),
        )}</div></details>`,
    )}<div class="section-gap" id="peers">${panel("同业与观察池比较", '<div class="empty">正在读取比较数据…</div>')}</div><div class="section-gap">${panel(
      "分析师一致预期",
      r.estimates.length
        ? table(
            ["指标", "预测期间", "发布时间", "统计值", "分析师数", "来源"],
            r.estimates.map(
              (e) =>
                `<tr><td>${escape(e.concept)}</td><td>${escape(e.period_end)}</td><td>${escape(e.published)}</td><td class="num">${fmt(Number(e.value))} ${escape(e.unit)} · ${escape(e.statistic)}</td><td class="num">${e.analyst_count}</td><td>${link(e.source_url)}</td></tr>`,
            ),
          )
        : '<div class="empty">尚无经授权导入的一致预期。自建预测保存在估值工作台，不会冒充市场共识。</div>',
    )}</div>`;
  drawCandles(r.bars);
  try {
    const peers = await api(`/api/peers?${qs()}`);
    if (g !== generation) return;
    const matches = peers.filter((p) => p.instrument.sector === i.sector);
    $("#peers").innerHTML = panel(
      "同业比较 · " + escape(i.sector),
      table(
        [
          "公司",
          "币种",
          "市值",
          "年度 P/E",
          "P/B",
          "OCF − CapEx 收益率",
          "净利率",
        ],
        matches.map(
          (p) =>
            `<tr class="clickable" data-instrument="${escape(p.instrument.id)}"><td><strong>${escape(p.instrument.symbol)}</strong><small>${escape(p.instrument.name)}</small></td><td>${escape(p.metrics.currency)}</td><td class="num">${compact(p.metrics.market_cap)}</td><td class="num">${fmt(p.metrics.pe)}</td><td class="num">${fmt(p.metrics.pb)}</td><td class="num">${pct(p.metrics.fcf_yield)}</td><td class="num">${pct(p.metrics.net_margin)}</td></tr>`,
        ),
      ) +
        `<div class="metric-note">限定相同行业。市值使用各自本币，不能直接比较绝对大小；会计口径与报告期可能不同。</div>`,
    );
  } catch (e) {
    toast(e.message, true);
  }
}
const concepts = {
  revenue: "营业收入",
  net_income: "净利润",
  operating_income: "营业利润",
  assets: "总资产",
  liabilities: "总负债",
  equity: "股东权益",
  cash: "现金及等价物",
  operating_cashflow: "经营现金流",
  capex: "资本开支",
  eps_diluted: "稀释每股收益",
  shares: "流通普通股股数",
  debt_current: "流动长期债务",
  debt_noncurrent: "非流动长期债务",
};
function renderValuation(r) {
  const i = r.instrument_info;
  $("#content").innerHTML =
    heading(
      "VALUATION / ASSUMPTION LAB",
      "估值工作台",
      `${escape(i.name)} · 明确假设、计算敏感性、保留每一个研究版本。`,
    ) +
    `<div class="grid equal-col">${panel("现金流折现 · FCFF", `<div class="panel-body"><form id="dcf-form" class="form-grid"><label>基期企业自由现金流 · 本币<input name="fcff" type="number" value="100000000" min="0.01" step="any" required></label><label>预测增长率 · %<input name="growth" type="number" value="5" min="-94" max="100" step="0.1" required></label><label>加权平均资本成本 · %<input name="wacc" type="number" value="10" min="0.01" max="100" step="any" required></label><label>永续增长率 · %<input name="terminal_growth" type="number" value="2" min="-49" max="99" step="0.1" required></label><label>净债务 · 本币（净现金可负）<input name="net_debt" type="number" value="0" step="any" required></label><label>普通股股数<input name="shares" type="number" value="100000000" min="0.01" step="any" required></label><label>预测年数<input name="years" type="number" value="5" min="1" max="20" required></label><label>模型币种<input name="currency" value="${escape(i.currency)}" readonly></label><div class="wide"><button class="primary" ${i.sector === "Financials" ? "disabled" : ""}>计算估值与敏感性</button><p class="help">默认数值只是输入示例，不来自公司财报。请自行构建未杠杆 FCFF，保持币种与单位一致。金融企业不适用此模型。</p></div></form></div>`)}${panel("估值结果", `<div class="panel-body" id="dcf-result"><div class="empty">输入你的财务假设后计算。<p class="help">每个结果可另存为版本，供日后复盘。</p></div></div>`)}</div><div id="sensitivity"></div><div class="section-gap">${panel(
      "已保存估值版本",
      r.valuations.length
        ? table(
            ["版本", "日期", "每股估值", "模型币种", "假设"],
            r.valuations.map(
              (v) =>
                `<tr><td>${escape(v.title)}</td><td>${escape(v.date)}</td><td class="num">${fmt(v.result?.per_share)}</td><td>${escape(v.assumptions.currency)}</td><td>增长 ${pct(v.assumptions.growth)} / WACC ${pct(v.assumptions.wacc)} / 永续 ${pct(v.assumptions.terminal_growth)}</td></tr>`,
            ),
          )
        : '<div class="empty">没有保存的估值版本</div>',
    )}</div>`;
}
let lastDCF = null;
function newsCards(news) {
  return (
    news
      .map(
        (n) =>
          `<article class="news-item"><div class="news-meta"><span>${escape(n.date)}</span><span>${escape(n.source)}</span>${n.instrument ? `<span class="pill">${escape(state.instruments.find((i) => i.id === n.instrument)?.symbol || n.instrument)}</span>` : ""}</div><h3>${escape(n.title)}</h3>${n.summary ? `<p>${escape(n.summary)}</p>` : ""}${link(n.source_url, "阅读来源 ↗")}</article>`,
      )
      .join("") ||
    '<div class="empty">没有已导入新闻或公告。支持规范化导入标题、摘要与原文链接。</div>'
  );
}
function renderNews(news, r) {
  $("#content").innerHTML =
    heading(
      "INTELLIGENCE / RESEARCH JOURNAL",
      "新闻与研究",
      "让信息连接到持仓，让投资论点留下可复盘的记录。",
    ) +
    `<div class="grid equal-col">${panel("市场与公司事件", newsCards(news), "已导入来源 · 非实时新闻流")}${panel(
      "投资论点 · " + escape(r.instrument_info.symbol),
      `<div class="panel-body"><form id="note-form" class="form-grid"><label class="wide">研究标题<input name="title" placeholder="投资逻辑 / 催化剂 / 风险因素" maxlength="200" required></label><label class="wide">论点与后续验证<textarea name="body" placeholder="记录证据、估值假设以及什么变化会推翻你的判断。" maxlength="2000" required></textarea></label><div class="wide"><button class="primary">保存研究笔记</button></div></form><div class="section-gap">${[
        ...r.notes,
      ]
        .reverse()
        .map(
          (n) =>
            `<article class="note"><div class="news-meta">${escape(n.date)}</div><h3>${escape(n.title)}</h3><p>${escape(n.body)}</p></article>`,
        )
        .join("")}</div></div>`,
    )}</div>`;
}
function renderData(d) {
  $("#content").innerHTML =
    heading(
      "DATA / PROVENANCE & RECONCILIATION",
      "数据中心",
      "所有数据留在本地。保留原始资料、导入历史与计算口径。",
      `<button id="export-data">导出完整工作区 ↓</button>`,
    ) +
    `<div class="data-counts">${[
      ["instruments", "证券"],
      ["trades", "成交"],
      ["cashflows", "现金流水"],
      ["facts", "财务版本"],
      ["quotes", "估值报价"],
      ["reconciliations", "对账快照"],
    ]
      .map(
        ([k, n]) =>
          `<div><strong>${d.counts[k]}</strong><small>${n}</small></div>`,
      )
      .join("")}</div><div class="grid equal-col">${panel(
      "导入文件",
      `<div class="panel-body"><form id="import-form" class="form-grid"><label class="wide">文件格式<select name="format"><option value="normalized">ReachTerminal 标准 JSON</option><option value="flex">IBKR Flex XML（现金股票）</option><option value="sec">SEC CompanyFacts JSON</option></select></label><label class="wide">SEC 对应证券 ID（仅 SEC 导入使用）<input name="instrument" value="${escape(state.selected || "")}" placeholder="先创建证券，例如 us:aapl"></label><label class="wide">选择文件<input class="file-input" name="file" type="file" accept=".json,.xml" required></label><div class="wide"><button class="primary">校验并原子导入</button><p class="help">相同编号与内容可重复导入；相同编号但内容改变会拒绝。需从完整历史或明确的期初账本开始。Flex 不会自动推算缺失入金或换汇。</p></div></form><details><summary>标准格式、字段和样例</summary><p class="help">参考仓库 docs/data-contract.md 和 examples/。金额与数量用字符串，最多 6 位小数。每条财务记录须注明披露日期、期间、单位和来源。</p><pre>{ "instruments": [...], "trades": [...],
  "cashflows": [...], "facts": [...],
  "quotes": [...], "fx": [...], "bars": [...] }</pre></details></div>`,
    )}${panel("创建证券与免费财务接入", `<div class="panel-body"><form id="instrument-form" class="form-grid"><label>唯一 ID<input name="id" placeholder="us:aapl" required></label><label>代码<input name="symbol" placeholder="AAPL" required></label><label>公司名称<input name="name" required></label><label>市场 / 交易所<input name="exchange" placeholder="XNAS" required></label><label>国家 / 地区<input name="country" placeholder="US" required></label><label>币种<input name="currency" placeholder="USD" pattern="[A-Z]{3}" required></label><label>SEC CIK（可选）<input name="sec_cik" placeholder="320193" pattern="[0-9]{1,10}"></label><label>行业<input name="sector" placeholder="Technology / Financials / Industrials" required></label><div class="wide"><button>创建证券</button></div></form><hr><form id="daily-fetch-form" class="form-grid"><label class="wide">日线证券 ID<input name="instrument" value="${escape(state.selected || "")}" required></label><div class="wide"><button>获取真实日线</button><p class="help">Alpha Vantage 原始日线，最近 100 个交易日，非实时。未配置个人免费密钥时仅 IBM 公开试用可用。密钥保留在服务端；额度或网络错误不会生成数据。修订冲突明确拒绝。</p></div></form><hr><form id="sec-fetch-form" class="form-grid"><label>证券 ID<input name="instrument" value="${escape(state.selected || "")}" required></label><label>SEC CIK<input name="cik" placeholder="320193" pattern="[0-9]{1,10}" required></label><div class="wide"><button>获取 SEC CompanyFacts</button><p class="help">需在后端设置 REACH_SEC_USER_AGENT（项目名与联系邮箱）。无密钥；网络受限时可下载 JSON 后导入。当前自动映射 US-GAAP / DEI；其他市场通过审核后的标准数据导入。</p></div></form></div>`)}</div><div class="grid equal-col">${panel(
      "导入审计",
      table(
        ["时间", "新增记录", "重复记录"],
        d.imports.map(
          (r) =>
            `<tr><td>${escape(r.created_at)}</td><td class="num">${r.inserted}</td><td class="num">${r.duplicates}</td></tr>`,
        ),
      ),
    )}${panel("能力与数据覆盖", `<div class="panel-body"><p class="help">● 已实现：本地账本、策略内 FIFO、拆股、汇率估值、对账、财务版本、用户 FCFF 估值、线性股票压力测试。</p><p class="help">○ 全球一致预期：支持带来源的导入，无内置完整免费数据源。</p><p class="help">○ IBKR 实时账户与行情：当前通过 Flex 文件接入；未连接 TWS / Gateway。</p><p class="help">○ 衍生品、经纪商保证金、自动全球财报标准化：后续阶段。</p><p class="help">代码开源不自动授权第三方数据再分发。演示数据为合成资料。</p>${Object.values(d.counts).every((n) => !n) ? '<button id="load-demo">加载合成演示数据</button>' : ""}</div>`)}</div>`;
}
function drawLine(id, points, currency = false) {
  const canvas = $("#" + id);
  if (!canvas) return;
  const draw = () => {
    const dpr = devicePixelRatio || 1,
      w = canvas.clientWidth,
      h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    const c = canvas.getContext("2d");
    c.scale(dpr, dpr);
    c.clearRect(0, 0, w, h);
    const finite = points.filter(
      (p) => p.value != null && Number.isFinite(p.value),
    );
    if (!finite.length) {
      c.fillStyle = "#8092a9";
      c.font = "12px sans-serif";
      c.fillText("暂无连续历史数据", 30, h / 2);
      return;
    }
    let min = Math.min(...finite.map((p) => p.value)),
      max = Math.max(...finite.map((p) => p.value));
    if (min === max) {
      min -= 1;
      max += 1;
    }
    const pad = { l: 14, r: 62, t: 22, b: 27 },
      range = max - min;
    min -= range * 0.12;
    max += range * 0.12;
    const x = (n) =>
        pad.l + (n / Math.max(1, points.length - 1)) * (w - pad.l - pad.r),
      y = (v) => pad.t + ((max - v) / (max - min)) * (h - pad.t - pad.b);
    c.font = "9px ui-monospace,monospace";
    for (let n = 0; n < 5; n++) {
      let v = min + ((max - min) * n) / 4,
        py = y(v);
      c.strokeStyle = "#223041";
      c.lineWidth = 0.6;
      c.beginPath();
      c.moveTo(pad.l, py);
      c.lineTo(w - pad.r, py);
      c.stroke();
      c.fillStyle = "#637b96";
      c.fillText(fmt(v, 1), w - pad.r + 9, py + 3);
    }
    c.strokeStyle = "#49cbbb";
    c.lineWidth = 1.7;
    c.beginPath();
    let open = false;
    points.forEach((p, n) => {
      if (p.value == null) {
        open = false;
        return;
      }
      if (!open) {
        c.moveTo(x(n), y(p.value));
        open = true;
      } else c.lineTo(x(n), y(p.value));
    });
    c.stroke();
    const f = finite[0],
      last = finite.at(-1);
    c.fillStyle = "#637b96";
    c.fillText(f.date, pad.l, h - 6);
    c.fillText(last.date, w - pad.r - 65, h - 6);
    canvas.onmousemove = (e) => {
      let n = Math.round(
        ((e.offsetX - pad.l) / (w - pad.l - pad.r)) *
          Math.max(1, points.length - 1),
      );
      n = Math.max(0, Math.min(points.length - 1, n));
      const p = points[n];
      $("#" + id + "-tip").textContent =
        `${p.date}  ·  ${currency ? money(p.value) : fmt(p.value)}`;
    };
  };
  draw();
  canvas._redraw = draw;
}
function drawCandles(bars, period = "all") {
  const canvas = $("#price-chart");
  if (!canvas) return;
  let b = bars;
  if (period !== "all") {
    const cutoff = new Date(state.asof + "T00:00:00Z");
    cutoff.setUTCDate(cutoff.getUTCDate() - Number(period));
    b = b.filter((x) => x.date >= cutoff.toISOString().slice(0, 10));
  }
  if (!b.length) {
    drawLine("price-chart", []);
    return;
  }
  const draw = () => {
    const dpr = devicePixelRatio || 1,
      w = canvas.clientWidth,
      h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    const c = canvas.getContext("2d");
    c.scale(dpr, dpr);
    const left = 14,
      right = 64,
      top = 20,
      bottom = 28;
    let min = Math.min(...b.map((x) => Number(x.low))),
      max = Math.max(...b.map((x) => Number(x.high)));
    if (min === max) {
      min -= 1;
      max += 1;
    }
    const step = (w - left - right) / b.length,
      y = (v) => top + ((max - v) / (max - min)) * (h - top - bottom);
    c.font = "9px ui-monospace,monospace";
    for (let n = 0; n < 5; n++) {
      let v = min + ((max - min) * n) / 4,
        py = y(v);
      c.strokeStyle = "#243142";
      c.beginPath();
      c.moveTo(left, py);
      c.lineTo(w - right, py);
      c.stroke();
      c.fillStyle = "#637b96";
      c.fillText(fmt(v), w - right + 9, py + 3);
    }
    b.forEach((v, n) => {
      const x = left + (n + 0.5) * step;
      const color = Number(v.close) >= Number(v.open) ? "#49cbbb" : "#f1808d";
      c.strokeStyle = color;
      c.fillStyle = color;
      c.beginPath();
      c.moveTo(x, y(Number(v.high)));
      c.lineTo(x, y(Number(v.low)));
      c.stroke();
      c.fillRect(
        x - Math.min(8, step * 0.6) / 2,
        Math.min(y(Number(v.open)), y(Number(v.close))),
        Math.max(1, Math.min(8, step * 0.6)),
        Math.max(1, Math.abs(y(Number(v.open)) - y(Number(v.close)))),
      );
    });
    c.fillStyle = "#637b96";
    c.fillText(b[0].date, left, h - 6);
    c.fillText(b.at(-1).date, w - right - 65, h - 6);
    canvas.onmousemove = (e) => {
      const n = Math.max(
        0,
        Math.min(b.length - 1, Math.floor((e.offsetX - left) / step)),
      );
      const v = b[n];
      $("#price-chart-tip").textContent =
        `${v.date} · O ${fmt(Number(v.open))} H ${fmt(Number(v.high))} L ${fmt(Number(v.low))} C ${fmt(Number(v.close))} · VOL ${compact(Number(v.volume))}`;
    };
  };
  draw();
  canvas._redraw = draw;
}
window.addEventListener("resize", () =>
  $$("canvas").forEach((c) => c._redraw?.()),
);
async function refresh() {
  const [ins, d] = await Promise.all([
    api("/api/instruments"),
    api("/api/data"),
  ]);
  state.instruments = ins;
  state.data = d;
  if (!ins.some((i) => i.id === state.selected)) state.selected = ins[0]?.id;
  $("#mode").textContent = d.demo_present
    ? "SYNTHETIC DEMO"
    : "LOCAL / PRIVATE";
  $("#mode").classList.toggle("demo", d.demo_present);
  $("#notice").classList.toggle("hidden", !d.demo_present);
  $("#notice").textContent =
    "合成演示工作区 · 公司、行情、财报与新闻均为虚构，不可用于投资判断。真实数据请使用独立数据库。";
  universe();
  await render();
}
document.addEventListener("click", async (e) => {
  const p = e.target.closest("[data-page]"),
    ins = e.target.closest("[data-instrument]");
  if (p) {
    state.page = p.dataset.page;
    lastDCF = null;
    render();
    return;
  }
  if (ins) {
    state.selected = ins.dataset.instrument;
    state.page = "research";
    universe();
    render();
    return;
  }
  if (e.target.closest("[data-period]")) {
    $$(".period-tabs button").forEach((b) =>
      b.classList.toggle("active", b === e.target),
    );
    drawCandles(state.research.bars, e.target.dataset.period);
    return;
  }
  try {
    if (e.target.id === "load-demo") {
      e.target.disabled = true;
      await post("/api/demo", {});
      toast("已加载合成演示数据");
      state.asof = "2026-10-09";
      $("#asof").value = state.asof;
      await refresh();
    }
    if (e.target.id === "export-data") {
      const data = await api("/api/export");
      const u = URL.createObjectURL(
        new Blob([JSON.stringify(data, null, 2)], { type: "application/json" }),
      );
      const a = document.createElement("a");
      a.href = u;
      a.download = `reachterminal-${state.asof}.json`;
      a.click();
      URL.revokeObjectURL(u);
    }
    if (e.target.id === "retry") render();
    if (e.target.id === "save-valuation" && lastDCF) {
      const title = $("#valuation-title").value.trim();
      if (!title) throw new Error("请填写版本名称");
      await post("/api/valuations", {
        instrument: state.selected,
        date: state.asof,
        title,
        assumptions: lastDCF.assumptions,
      });
      toast("估值版本已保存");
      render();
    }
  } catch (err) {
    toast(err.message, true);
    if (e.target.isConnected) e.target.disabled = false;
  }
});
document.addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = e.target,
    button = $("button", f);
  if (button) button.disabled = true;
  try {
    const v = Object.fromEntries(new FormData(f));
    if (f.id === "stress-form") {
      const r = await post("/api/stress", {
        as_of: state.asof,
        equity_shock: Number(v.equity_shock) / 100,
        fx_shock: Number(v.fx_shock) / 100,
      });
      $("#stress-result").innerHTML =
        `<div class="scenario-result"><span class="stat-label">情景损益 · USD</span><div class="stat-value ${sign(r.pnl)}">${money(r.pnl)}</div><p class="help">冲击后净资产 ${money(r.nav_after)} · 未计算保证金</p></div>`;
    }
    if (f.id === "dcf-form") {
      const a = { ...v, sector: state.research.instrument_info.sector };
      for (const k of [
        "fcff",
        "growth",
        "wacc",
        "terminal_growth",
        "net_debt",
        "shares",
        "years",
      ])
        a[k] = Number(a[k]);
      for (const k of ["growth", "wacc", "terminal_growth"]) a[k] /= 100;
      const r = await post("/api/dcf", a);
      lastDCF = r;
      $("#dcf-result").innerHTML =
        `<div class="eyebrow">INTRINSIC VALUE / ${escape(r.currency)}</div><div class="result-number">${fmt(r.per_share)}</div><p class="sub">每股估值 · 基于你的假设</p><div class="section-gap"><div class="exposure-row"><span>企业价值</span><strong>${compact(r.enterprise_value)}</strong></div><div class="exposure-row"><span>股权价值</span><strong>${compact(r.equity_value)}</strong></div><div class="exposure-row"><span>终值现值占比</span><strong>${pct(r.terminal_share)}</strong></div></div><p class="help">终值占比较高时，结果对折现率和永续增长假设尤其敏感。</p><input id="valuation-title" placeholder="版本名称，如：基准情景 2026Q3" maxlength="150" aria-label="估值版本名称"><button id="save-valuation" class="section-gap">保存估值版本</button>`;
      const ws = [a.wacc - 0.01, a.wacc, a.wacc + 0.01],
        gs = [
          a.terminal_growth - 0.005,
          a.terminal_growth,
          a.terminal_growth + 0.005,
        ];
      const results = await Promise.all(
        ws.map((w) =>
          Promise.all(
            gs.map((t) =>
              w > t && w > 0
                ? post("/api/dcf", { ...a, wacc: w, terminal_growth: t })
                : Promise.resolve(null),
            ),
          ),
        ),
      );
      $("#sensitivity").innerHTML = panel(
        "敏感性 · 每股估值",
        table(
          ["WACC / 永续增长", ...gs.map(pct)],
          ws.map(
            (w, n) =>
              `<tr class="sensitivity"><td>${pct(w)}</td>${results[n].map((x, k) => `<td class="${n === 1 && k === 1 ? "center-cell" : ""}">${fmt(x?.per_share)}</td>`).join("")}</tr>`,
          ),
        ),
      );
    }
    if (f.id === "note-form") {
      await post("/api/notes", {
        ...v,
        instrument: state.selected,
        date: state.asof,
      });
      toast("研究笔记已保存");
      await render();
    }
    if (f.id === "instrument-form") {
      if (!v.sec_cik) delete v.sec_cik;
      await post("/api/import", {
        instruments: [{ ...v, asset_class: "STK" }],
      });
      state.selected = v.id;
      toast("证券已创建");
      await refresh();
    }
    if (f.id === "daily-fetch-form") {
      toast("正在获取真实日线…");
      const result = await post("/api/market/daily", v);
      toast(`日线已导入 · 最新交易日 ${result.latest}`);
      await refresh();
    }
    if (f.id === "sec-fetch-form") {
      toast("正在从 SEC 获取公开财务数据…");
      await post("/api/sec/fetch", v);
      toast("SEC 财务数据已导入");
      await refresh();
    }
    if (f.id === "import-form") {
      const file = $("[name=file]", f).files[0];
      if (!file) throw new Error("请选择文件");
      if (file.size > 32 * 1024 * 1024) throw new Error("文件超过 32 MB");
      const body = await file.text();
      let path = "/api/import";
      if (v.format === "flex") path += "/flex";
      if (v.format === "sec") {
        if (!v.instrument) throw new Error("请填写已创建的证券 ID");
        path += "/sec?instrument=" + encodeURIComponent(v.instrument);
      }
      const result = await api(path, {
        method: "POST",
        body,
        raw: true,
        headers: {
          "Content-Type":
            v.format === "flex" ? "application/xml" : "application/json",
        },
      });
      toast(`导入完成：新增 ${result.inserted}，重复 ${result.duplicates}`);
      await refresh();
    }
  } catch (err) {
    toast(err.message, true);
  } finally {
    if (button?.isConnected) button.disabled = false;
  }
});
$("#asof").value = state.asof;
$("#asof").addEventListener("change", (e) => {
  if (e.target.value) {
    state.asof = e.target.value;
    render();
  }
});
$("#search").addEventListener("input", universe);
$("#symbol-picker").addEventListener("change", (e) => {
  state.selected = e.target.value;
  state.page = "research";
  universe();
  render();
});
$("#refresh").addEventListener("click", () =>
  refresh().catch((e) => toast(e.message, true)),
);
document.addEventListener("keydown", (e) => {
  if (["INPUT", "TEXTAREA", "SELECT"].includes(document.activeElement.tagName))
    return;
  if (e.key === "/") {
    e.preventDefault();
    $("#search").focus();
  }
  if (/^[1-6]$/.test(e.key)) {
    state.page = Object.keys(names)[Number(e.key) - 1];
    render();
  }
});
try {
  const session = await api("/api/session");
  state.token = session.token;
  await refresh();
} catch (e) {
  $("#content").innerHTML = panel(
    "连接失败",
    `<div class="empty"><p>${escape(e.message)}</p><p>请启动本地 C++ 服务与 PostgreSQL。</p></div>`,
  );
}
