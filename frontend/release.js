export async function releasePage(ctx) {
  const {
    state,
    api,
    post,
    escape: E,
    fmt: F,
    panel,
    heading,
    toast,
    generationValid,
  } = ctx;
  const content = document.querySelector("#content");
  const $ = (s) => content.querySelector(s);
  const label = (name, title, value, type = "number", extra = "") =>
    `<label>${E(title)}<input name="${name}" type="${type}" ${type === "number" ? 'step="any"' : ""} value="${E(value)}" ${extra}></label>`;
  if (state.page === "hedge") {
    const models = await api(`/api/hedge/models?as_of=${state.asof}`);
    if (!generationValid()) return;
    content.innerHTML =
      heading(
        "DERIVATIVES / SCENARIO LAB",
        "衍生品对冲",
        "独立假设组合 · 股票、欧式期权、线性期货 · 与现金账本分开核算",
      ) +
      panel(
        "组合假设",
        `<div class="panel-body"><p class="help">以下为可编辑的假设案例，未读取账户持仓。期权采用 Black–Scholes 连续收益率模型；美式期权、提前行权、波动率曲面和期货基差尚未建模。</p><form id="hedge-form"><div class="form-grid">${label("title", "方案名称", "保护性看跌示例", "text", 'required maxlength="200"')}<label>组合币种<select name="currency"><option>USD</option><option>EUR</option><option>HKD</option><option>CNY</option></select></label>${label("shock", "价格冲击 %", -10)}${label("volshock", "波动率变化（百分点）", 5)}${label("days", "时间流逝（天）", 7)}</div><div id="hedge-rows"></div><button type="button" id="add-hedge">＋ 增加假设持仓</button> <button class="primary">计算对冲与情景</button> <button type="button" id="save-hedge">保存方案版本</button></form><div class="section-gap"><label>已保存方案<select id="saved-hedge"><option value="">选择方案</option>${models.map((m, i) => `<option value="${i}">${E(m.date)} · ${E(m.title)}</option>`).join("")}</select></label></div></div>`,
      ) +
      `<div id="hedge-result" class="section-gap"></div>`;
    let counter = 0;
    const add = (
      p = {
        type: "STK",
        underlying: "MODEL",
        spot: 100,
        quantity: 100,
        multiplier: 1,
      },
    ) => {
      const box = document.createElement("div");
      box.className = "hedge-row section-gap";
      box.dataset.id = p.id || `model-${Date.now()}-${++counter}`;
      box.innerHTML = `<div class="form-grid"><label>类别<select name="type">${["STK", "OPT", "FUT"].map((t) => `<option ${t === p.type ? "selected" : ""}>${t}</option>`).join("")}</select></label>${label("underlying", "同一标的标识", p.underlying, "text", "required")}${label("spot", "现价 / 期货合约价格", p.spot)}${label("quantity", "数量（空头为负）", p.quantity)}${label("multiplier", "合约乘数", p.multiplier)}<label>期权方向<select name="right"><option value="call">看涨</option><option value="put" ${p.right === "put" ? "selected" : ""}>看跌</option></select></label>${label("strike", "行权价", p.strike ?? 100)}${label("years", "剩余期限（年）", p.years ?? 0.5)}${label("volatility", "年化波动率 %", (p.volatility ?? 0.25) * 100)}${label("rate", "无风险利率 %", (p.rate ?? 0.04) * 100)}${label("yield", "连续收益率 %", (p.yield ?? 0) * 100)}</div><button type="button" class="remove-hedge">删除这笔假设</button>`;
      const update = () => {
        const option = box.querySelector("[name=type]").value === "OPT";
        for (const n of [
          "right",
          "strike",
          "years",
          "volatility",
          "rate",
          "yield",
        ])
          box.querySelector(`[name=${n}]`).disabled = !option;
      };
      box.querySelector("[name=type]").onchange = update;
      update();
      box.querySelector(".remove-hedge").onclick = () => box.remove();
      $("#hedge-rows").append(box);
    };
    add();
    add({
      type: "OPT",
      underlying: "MODEL",
      spot: 100,
      quantity: 1,
      multiplier: 100,
      right: "put",
    });
    $("#add-hedge").onclick = () => add();
    const book = () => {
      const form = $("#hedge-form");
      const val = (n) => Number(form.elements[n].value);
      return {
        currency: form.elements.currency.value,
        scenario: {
          spot: val("shock") / 100,
          volatility: val("volshock") / 100,
          days: val("days"),
        },
        positions: [...content.querySelectorAll(".hedge-row")].map((row) => {
          const get = (n) => row.querySelector(`[name=${n}]`).value;
          const p = {
            id: row.dataset.id,
            type: get("type"),
            underlying: get("underlying"),
            spot: Number(get("spot")),
            quantity: Number(get("quantity")),
            multiplier: Number(get("multiplier")),
          };
          if (p.type === "OPT") {
            for (const n of ["strike", "years", "volatility", "rate", "yield"])
              p[n] =
                Number(get(n)) /
                (["volatility", "rate", "yield"].includes(n) ? 100 : 1);
            p.right = get("right");
            p.style = "European";
          }
          return p;
        }),
      };
    };
    const show = (r) => {
      $("#hedge-result").innerHTML = panel(
        "模型结果",
        `<div class="panel-body"><div class="grid stats"><div>模型市值 · ${E(r.currency)}<h2>${F(r.market_value)}</h2></div><div>情景损益 · ${E(r.currency)}<h2>${F(r.scenario_pnl)}</h2></div><div>Vega / +1百分点<h2>${F(r.vega_1pp)}</h2></div><div>Theta / 日<h2>${F(r.theta_day)}</h2></div></div><div class="table-wrap"><table><thead><tr><th>标的</th><th>Delta 单位</th><th>Delta 名义敞口</th><th>Gamma × S² / 2</th><th>理论股票对冲数量</th></tr></thead><tbody>${Object.entries(
          r.underlyings,
        )
          .map(
            ([u, g]) =>
              `<tr><td>${E(u)}</td><td>${F(g.delta_units, 4)}</td><td>${F(g.delta_notional)}</td><td>${F(g.gamma_half_notional)}</td><td>${F(g.delta_neutral_stock_units, 4)}</td></tr>`,
          )
          .join(
            "",
          )}</tbody></table></div><p class="help">对冲数量仅中和当前模型 Delta；并非交易建议或保证金估计。情景采用全量重新定价，正负号按持仓方向计算。</p></div>`,
      );
    };
    $("#hedge-form").onsubmit = async (e) => {
      e.preventDefault();
      try {
        const r = await post("/api/hedge/analyze", book());
        if (generationValid()) show(r);
      } catch (e) {
        if (generationValid()) toast(e.message, true);
      }
    };
    $("#save-hedge").onclick = async () => {
      try {
        await post("/api/hedge/models", {
          date: state.asof,
          title: $("#hedge-form").elements.title.value,
          assumptions: book(),
        });
        if (generationValid()) toast("方案版本已保存；刷新可加载");
      } catch (e) {
        toast(e.message, true);
      }
    };
    $("#saved-hedge").onchange = (e) => {
      if (e.target.value === "") return;
      const m = models[Number(e.target.value)],
        f = $("#hedge-form");
      f.elements.title.value = m.title;
      f.elements.currency.value = m.assumptions.currency;
      f.elements.shock.value = m.assumptions.scenario.spot * 100;
      f.elements.volshock.value = m.assumptions.scenario.volatility * 100;
      f.elements.days.value = m.assumptions.scenario.days;
      $("#hedge-rows").replaceChildren();
      m.assumptions.positions.forEach(add);
      show(m.result);
    };
  } else if (state.page === "macro") {
    const r = await api(`/api/macro?as_of=${state.asof}`);
    if (!generationValid()) return;
    const titles = {
      gdp: "名义 GDP",
      growth: "实际 GDP 增速",
      inflation: "居民消费价格通胀",
    };
    content.innerHTML =
      heading(
        "GLOBAL / MACRO OBSERVATORY",
        "宏观数据",
        "世界银行 WDI · 免费官方年度数据 · 保留抓取版本",
      ) +
      panel(
        "获取官方数据",
        `<div class="panel-body"><form id="macro-form" class="form-grid"><label>国家 / 地区<select name="country">${Object.entries(
          {
            USA: "美国",
            CHN: "中国",
            DEU: "德国",
            JPN: "日本",
            GBR: "英国",
            HKG: "中国香港",
            FRA: "法国",
            IND: "印度",
            BRA: "巴西",
            AUS: "澳大利亚",
          },
        )
          .map(([k, v]) => `<option value="${k}">${v}</option>`)
          .join(
            "",
          )}</select></label><label>指标<select name="indicator">${Object.entries(
          titles,
        )
          .map(([k, v]) => `<option value="${k}">${v}</option>`)
          .join(
            "",
          )}</select></label><div><button class="primary">抓取年度数据</button></div></form><p class="help">报告期与发布日期不同。WDI 未提供每笔历史发布日期，本平台从实际抓取日起允许历史分析使用；缺失值保持为空。抓取范围 2010–2025，可在分析日期中检查可见版本。</p></div>`,
      ) +
      panel(
        "年度观测",
        `<div class="table-wrap"><table><thead><tr><th>国家</th><th>指标</th><th>报告期</th><th>值</th><th>单位</th><th>抓取日期</th><th>来源</th></tr></thead><tbody>${
          r.observations
            .slice()
            .reverse()
            .map(
              (o) =>
                `<tr><td>${E(o.country)}</td><td>${E(titles[o.indicator])}</td><td>${E(o.period_end)}</td><td>${F(o.value)}</td><td>${E(o.unit)}</td><td>${E(o.date)}</td><td><a href="${E(o.source_url)}" target="_blank" rel="noopener noreferrer">WDI ↗</a></td></tr>`,
            )
            .join("") ||
          '<tr><td colspan="7">当前分析日期没有已抓取数据</td></tr>'
        }</tbody></table></div>`,
        `${r.history.length} 个历史版本`,
      );
    const series = [
      ...new Set(r.observations.map((o) => `${o.country}/${o.indicator}`)),
    ];
    if (series.length) {
      const block = document.createElement("div");
      block.className = "section-gap";
      block.innerHTML = panel(
        "年度趋势",
        `<div class="panel-body"><label>图表序列<select id="macro-series">${series.map((k) => `<option value="${E(k)}">${E(k.split("/")[0])} · ${E(titles[k.split("/")[1]])}</option>`).join("")}</select></label></div>${ctx.chart("macro-chart", "GDP 以万亿美元显示；增速和通胀以百分比显示。缺失年度不连线。")}`,
      );
      content.append(block);
      const draw = () => {
        const key = $("#macro-series").value;
        ctx.drawLine(
          "macro-chart",
          r.observations
            .filter((o) => `${o.country}/${o.indicator}` === key)
            .sort((a, b) => a.period_end.localeCompare(b.period_end))
            .map((o) => ({
              date: o.period_end,
              value:
                o.value == null
                  ? null
                  : o.value / (o.indicator === "gdp" ? 1e12 : 1),
            })),
        );
      };
      $("#macro-series").onchange = draw;
      draw();
    }
    $("#macro-form").onsubmit = async (e) => {
      e.preventDefault();
      const f = e.target,
        b = f.querySelector("button");
      b.disabled = true;
      try {
        await post("/api/macro/fetch", {
          country: f.elements.country.value,
          indicator: f.elements.indicator.value,
        });
        if (generationValid()) ctx.reload();
      } catch (e) {
        if (generationValid()) {
          toast(e.message, true);
          b.disabled = false;
        }
      }
    };
  } else if (state.page === "margin") {
    content.innerHTML =
      heading(
        "BROKER / MARGIN MONITOR",
        "保证金监控",
        "本设备 IBKR 会话 · 手动读取券商账户汇总",
      ) +
      panel(
        "账户连接",
        `<div class="panel-body"><p class="help">先在数据中心登录本机网关。账户与保证金只在当前页面读取，不存入研究数据库，不参与内部模型估算。</p><button id="margin-accounts" class="primary">读取本机账户列表</button><form id="margin-form" class="form-grid section-gap"><label>账户<select name="account" required><option value="">请先读取账户</option></select></label><div><button>读取保证金</button></div></form><p class="help">最短刷新间隔 10 秒；券商时间戳超过 120 秒会标记为过期。10% 剩余流动性比例仅为本地提醒阈值。</p></div>`,
      ) +
      `<div id="margin-result" class="section-gap"></div>`;
    $("#margin-accounts").onclick = async () => {
      try {
        const rows = await post("/api/ibkr/accounts", {});
        if (!generationValid()) return;
        $("#margin-form").elements.account.innerHTML = rows
          .map(
            (a) =>
              `<option value="${E(a.id)}">${E(a.id)} · ${E(a.currency || "币种未知")}</option>`,
          )
          .join("");
      } catch (e) {
        if (generationValid()) toast(e.message, true);
      }
    };
    $("#margin-form").onsubmit = async (e) => {
      e.preventDefault();
      $("#margin-result").textContent = "正在读取券商保证金…";
      try {
        const r = await post("/api/ibkr/margin", {
          account: e.target.elements.account.value,
        });
        if (!generationValid()) return;
        const status = {
          unknown: "数据不足 / 状态未知",
          critical: "剩余流动性不足",
          reported: "券商已报告",
          "low-buffer": "流动性缓冲偏低",
        };
        $("#margin-result").innerHTML = panel(
          status[r.status] || "未知状态",
          `<div class="panel-body"><p>剩余流动性 / 净清算值：${r.liquidity_ratio == null ? "—" : F(r.liquidity_ratio * 100) + "%"} · 读取时间 ${E(new Date(r.retrieved_at_ms).toLocaleString())}</p><div class="table-wrap"><table><thead><tr><th>券商字段</th><th>值</th><th>币种</th><th>券商更新时间</th><th>时效</th></tr></thead><tbody>${r.fields.map((v) => `<tr><td>${E(v.label)}</td><td>${F(v.value)}</td><td>${E(v.currency || "未知")}</td><td>${v.broker_timestamp ? E(new Date(v.broker_timestamp).toLocaleString()) : "未知"}</td><td>${v.stale ? "过期 / 未知" : "近期"}</td></tr>`).join("")}</tbody></table></div><p class="help">使用券商汇总值，不叠加证券及商品分项。账户购买力不等于现金；本页不能推导强平价格。</p></div>`,
        );
      } catch (e) {
        if (generationValid()) {
          $("#margin-result").textContent = "本次读取失败；保证金状态未知。";
          toast(e.message, true);
        }
      }
    };
  }
}
