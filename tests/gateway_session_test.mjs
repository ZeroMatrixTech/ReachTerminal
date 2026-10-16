import assert from "node:assert/strict";
import {
  sessionMessage,
  createGatewayMonitor,
} from "../frontend/gateway-session.js";
let checks = 0;
const check = (x) => {
  assert.ok(x);
  checks++;
};
check(sessionMessage({ authenticated: true }).kind === "success");
check(
  sessionMessage({ sso_valid: true, authenticated: false }).kind === "partial",
);
check(sessionMessage({ sso_valid: false }).kind === "pending");
check(sessionMessage({}).kind === "error");
check(
  sessionMessage({ authenticated: true, competing: true }).kind === "warning",
);
let resolve,
  calls = 0,
  display = [],
  clock = 0,
  current = true,
  tasks = [];
const monitor = createGatewayMonitor({
  post: () => {
    calls++;
    return new Promise((r) => (resolve = r));
  },
  display: (m) => display.push(m),
  isCurrent: () => current,
  now: () => clock,
  schedule: (f, ms) => {
    tasks.push({ f, ms });
    return tasks.length;
  },
  cancel: () => {},
  visible: () => true,
});
const flush = () => new Promise((r) => setImmediate(r));
monitor.start();
monitor.focus();
check(calls === 1);
monitor.stop();
resolve({ authenticated: true });
await flush();
check(!display.some((m) => m.kind === "success"));
check(tasks.length === 0);
monitor.start();
resolve({ sso_valid: true, authenticated: false });
await flush();
check(display.at(-1).kind === "partial");
clock = 180001;
await tasks.at(-1).f();
check(display.at(-1).text.includes("超时"));
check(calls === 2);
current = false;
await monitor.check();
check(calls === 2);
let hiddenCalls = 0;
const hidden = createGatewayMonitor({
  post: async () => {
    hiddenCalls++;
    return { authenticated: true };
  },
  display: () => {},
  isCurrent: () => true,
  visible: () => false,
});
hidden.start();
check(hiddenCalls === 0);
hidden.stop();
console.log(`${checks} gateway UI state checks passed`);

clock = 200000;
current = true;
display = [];
monitor.start();
clock = 380001;
resolve({ authenticated: true });
await flush();
check(display.at(-1).text.includes("超时"));
check(!display.some((m) => m.kind === "success"));
console.log("2 delayed-response deadline checks passed");
let done,
  failed,
  observed = [];
const stable = createGatewayMonitor({
  post: () =>
    new Promise((r, j) => {
      done = r;
      failed = j;
    }),
  display: (m) => observed.push(m),
  isCurrent: () => true,
  visible: () => true,
});
let result = stable.check();
done({ sso_valid: true, authenticated: false });
await result;
result = stable.check();
check(observed.at(-1).kind === "partial");
check(observed.at(-1).text.includes("复查"));
failed(new Error("synthetic offline"));
await result;
check(observed.at(-1).kind === "error");
result = stable.check();
check(observed.at(-1).kind === "error");
stable.stop();
done({ authenticated: true });
await result;
console.log("4 retained-status/error refresh checks passed");

check(
  sessionMessage({ initializing: true, sso_valid: true, authenticated: false })
    .kind === "initializing",
);
check(
  sessionMessage({ initializing: true, authenticated: true }).kind ===
    "success",
);
console.log("2 warm-up state checks passed");
check(
  sessionMessage({
    sso_valid: true,
    authenticated: true,
    initializing: true,
    market_ready: false,
  }).kind === "initializing",
);
console.log("1 account-bootstrap readiness check passed");
