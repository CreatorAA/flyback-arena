(() => {
  "use strict";

  const MODE_LABELS = {
    duel: "1v1 单挑",
    ffa3: "1v1v1 三人乱斗",
    ffa4: "1v1v1v1 四人乱斗",
  };

  const ROOM_STATE_LABELS = {
    ready: "准备中",
    lobby: "准备中",
    loading: "载入中",
    countdown: "倒计时",
    playing: "进行中",
    paused: "已暂停",
    settling: "结算中",
    finished: "已结束",
  };

  const ROLE_LABELS = {
    viewer: "只读员 · viewer",
    operator: "值班员 · operator",
    admin: "管理员 · admin",
  };

  const CAP_FIELD_META = [
    { key: "max_rooms", label: "最大房间数", scope: "立即对新建房生效；不删除现存房间" },
    { key: "max_active_matches", label: "最大活跃对局", scope: "立即对新开局生效；进行中/暂停局保留" },
    { key: "max_spectators_per_room", label: "每房默认观众上限", scope: "立即对新入场观众生效；存量观众不断线" },
    { key: "spectator_snapshot_hz", label: "默认观战下发频率 (Hz)", scope: "下一推送周期生效；参赛席仍逐步接收状态" },
    { key: "egress_budget_kbps", label: "出站带宽预算 (kbps)", scope: "0 表示不限；持续超预算先降观战频率，再限制新观众" },
    { key: "idle_room_ttl_seconds", label: "无人空房回收 (秒)", scope: "≥ 0 合法；不回收仍有观众的 AI 表演赛" },
    { key: "allow_room_creation", label: "允许玩家新建房间", scope: "立即对新建房请求生效；现存房间不受影响" },
  ];

  const state = {
    session: null,
    server: null,
    activeTab: "overview",
    selectedRoomId: null,
    mobileDetailOpen: false,
    filters: { mode: "all", state: "all", lock: "all", search: "" },
    replaySearch: "",
    logSearch: "",
    es: null,
    sseConnected: false,
    pendingCommands: new Map(),
    bannerTimer: null,
    capDraft: null,
    capDraftRev: 1,
    capDirty: false,
    roomDrafts: new Map(),
    confirmKey: null,
    conflictContext: null,
    loginLockTimer: null,
  };

  const $ = (id) => document.getElementById(id);

  function el(tag, attrs = {}, children = []) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v === undefined || v === null) continue;
      if (k === "class") node.className = v;
      else if (k === "text") node.textContent = String(v);
      else if (k.startsWith("on") && typeof v === "function") {
        node.addEventListener(k.slice(2).toLowerCase(), v);
      } else if (typeof v === "boolean") {
        if (v) node.setAttribute(k, "");
      } else {
        node.setAttribute(k, String(v));
      }
    }
    for (const ch of Array.isArray(children) ? children : [children]) {
      if (ch === null || ch === undefined) continue;
      node.appendChild(typeof ch === "string" ? document.createTextNode(ch) : ch);
    }
    return node;
  }

  function clearNode(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  function canOperate() {
    const r = state.session && state.session.role;
    return r === "operator" || r === "admin";
  }

  function canAdmin() {
    return Boolean(state.session && state.session.role === "admin");
  }

  function getGlobalRevision() {
    if (!state.server) return 1;
    if (state.server.config_revision !== undefined) return Number(state.server.config_revision);
    if (state.server.revision !== undefined) return Number(state.server.revision);
    return 1;
  }

  function findRoom(roomId) {
    if (!state.server || !Array.isArray(state.server.rooms)) return null;
    return state.server.rooms.find((r) => r.room_id === roomId) || null;
  }

  function getRoomRevision(room) {
    if (!room) return 0;
    if (room.config_revision !== undefined) return Number(room.config_revision);
    if (room.room_rev !== undefined) return Number(room.room_rev);
    return 0;
  }

  function getResourceRevision(target) {
    if (typeof target === "string" && target.startsWith("room:")) {
      return getRoomRevision(findRoom(target.slice(5)));
    }
    return getGlobalRevision();
  }

  async function apiFetch(url, options = {}) {
    const headers = new Headers(options.headers || {});
    if (options.json !== undefined) {
      headers.set("Content-Type", "application/json");
    }
    if (options.method && options.method !== "GET" && state.session && state.session.csrf_token) {
      headers.set("X-CSRF-Token", state.session.csrf_token);
    }
    const resp = await fetch(url, {
      method: options.method || "GET",
      headers,
      credentials: "same-origin",
      body: options.json !== undefined ? JSON.stringify(options.json) : undefined,
    });
    let data = null;
    try {
      data = await resp.json();
    } catch {
      data = null;
    }
    return { ok: resp.ok, status: resp.status, data };
  }

  // ---------- Gate / Session ----------
  async function boot() {
    bindStaticEvents();
    await loadSession();
  }

  async function loadSession() {
    const res = await apiFetch("/api/v1/session");
    if (!res.ok || !res.data) {
      showGateError("无法连接到管理接口，请确认专服已启用管理端。");
      return;
    }
    state.session = res.data;
    if (res.data.server_name) {
      $("gate-server-meta").textContent = `${res.data.server_name} · ${res.data.proto_version || "fbar"}`;
    }
    if (res.data.bootstrap_required) {
      showGate("bootstrap");
      closeSSE();
      return;
    }
    if (!res.data.authenticated) {
      showGate("login");
      closeSSE();
      return;
    }
    showConsole();
    await refreshFullState({ resetDraft: true });
    connectSSE();
  }

  function showGate(mode) {
    $("console-view").classList.add("hidden");
    $("gate-view").classList.remove("hidden");
    $("bootstrap-box").classList.toggle("hidden", mode !== "bootstrap");
    $("login-form").classList.toggle("hidden", mode !== "login");
    $("login-error").classList.add("hidden");
  }

  function showGateError(msg) {
    showGate("login");
    const box = $("login-error");
    box.textContent = msg;
    box.classList.remove("hidden");
  }

  function showConsole() {
    $("gate-view").classList.add("hidden");
    $("console-view").classList.remove("hidden");
    const role = state.session.role || "viewer";
    $("hdr-username").textContent = state.session.username || "—";
    const roleBadge = $("hdr-role");
    roleBadge.textContent = ROLE_LABELS[role] || role;
    roleBadge.className = `role-badge role-${role}`;
  }

  async function handleLogin(ev) {
    ev.preventDefault();
    const btn = $("btn-login-submit");
    const errBox = $("login-error");
    errBox.classList.add("hidden");
    btn.disabled = true;

    const username = $("login-username").value.trim();
    const password = $("login-password").value;
    const res = await apiFetch("/api/v1/auth/login", {
      method: "POST",
      json: { username, password },
    });
    $("login-password").value = "";

    if (res.ok && res.data && res.data.authenticated) {
      btn.disabled = false;
      state.session = res.data;
      showConsole();
      await refreshFullState({ resetDraft: true });
      connectSSE();
      return;
    }

    if (res.status === 429 && res.data && res.data.retry_after_seconds) {
      startLoginBackoff(res.data.retry_after_seconds, res.data.message);
      return;
    }

    btn.disabled = false;
    errBox.textContent = (res.data && res.data.message) || "登录失败，请核对账号与口令。";
    errBox.classList.remove("hidden");
  }

  function startLoginBackoff(seconds, baseMsg) {
    if (state.loginLockTimer) clearInterval(state.loginLockTimer);
    let left = Math.max(1, Number(seconds) || 5);
    const btn = $("btn-login-submit");
    const errBox = $("login-error");
    const tick = () => {
      errBox.textContent = baseMsg || `登录尝试过多，请 ${left} 秒后再试。`;
      errBox.classList.remove("hidden");
      btn.disabled = true;
      if (left <= 0) {
        clearInterval(state.loginLockTimer);
        state.loginLockTimer = null;
        btn.disabled = false;
      }
      left -= 1;
    };
    tick();
    state.loginLockTimer = setInterval(tick, 1000);
  }

  async function handleLogout() {
    await apiFetch("/api/v1/auth/logout", { method: "POST" });
    clearPrivateView();
    await loadSession();
  }

  function clearPrivateView() {
    closeSSE();
    state.server = null;
    state.capDraft = null;
    state.capDirty = false;
    state.roomDrafts.clear();
    state.pendingCommands.clear();
    state.confirmKey = null;
    state.selectedRoomId = null;
    state.conflictContext = null;
    $("conflict-modal").classList.add("hidden");
  }

  // ---------- Full State & SSE ----------
  async function refreshFullState(opts = {}) {
    const res = await apiFetch("/api/v1/state");
    if (res.status === 401) {
      await loadSession();
      return;
    }
    if (!res.ok || !res.data) {
      showCommandBanner("failed", "拉取服务端状态失败，请稍后重试。");
      return;
    }
    state.server = res.data;
    if (!state.selectedRoomId && res.data.rooms && res.data.rooms.length) {
      state.selectedRoomId = res.data.rooms[0].room_id;
    }
    if (opts.resetDraft || !state.capDirty) {
      syncCapDraftFromServer();
    }
    pruneCleanRoomDrafts();
    renderAll();
  }

  function syncCapDraftFromServer() {
    if (!state.server) return;
    const c = state.server.capacity || {};
    const s = state.server.server || {};
    state.capDraft = {
      max_rooms: c.max_rooms ?? 16,
      max_active_matches: c.max_active_matches ?? 4,
      max_spectators_per_room: c.max_spectators_per_room ?? 32,
      spectator_snapshot_hz: c.spectator_snapshot_hz ?? 10,
      egress_budget_kbps: s.egress_budget_kbps !== undefined ? s.egress_budget_kbps : 0,
      idle_room_ttl_seconds: c.idle_room_ttl_seconds !== undefined ? c.idle_room_ttl_seconds : 300,
      allow_room_creation: Boolean(s.allow_room_creation),
    };
    state.capDraftRev = getGlobalRevision();
    state.capDirty = false;
    populateCapacityInputs();
  }

  function pruneCleanRoomDrafts() {
    for (const [rid, draft] of state.roomDrafts.entries()) {
      if (!draft.dirty || !findRoom(rid)) {
        state.roomDrafts.delete(rid);
      }
    }
  }

  function getRoomDraft(room) {
    const existing = state.roomDrafts.get(room.room_id);
    if (existing && existing.dirty) return existing;
    return {
      next_map_id: room.next_map_id || room.map_id || "",
      spectator_limit: room.effective_spectator_limit !== undefined ? room.effective_spectator_limit : 32,
      spectator_snapshot_hz: room.spectator_snapshot_hz !== undefined ? room.spectator_snapshot_hz : 10,
      dirty: false,
      startRev: getRoomRevision(room),
    };
  }

  function connectSSE() {
    closeSSE();
    const cursor = state.server ? state.server.event_cursor || 0 : 0;
    const es = new EventSource(`/api/v1/events?after=${encodeURIComponent(cursor)}`);
    state.es = es;

    es.onopen = () => {
      state.sseConnected = true;
      renderSSEStatus();
    };

    es.addEventListener("telemetry", (ev) => {
      state.sseConnected = true;
      const d = safeParse(ev.data);
      if (!d || !state.server) return;
      if (d.event_cursor !== undefined) state.server.event_cursor = d.event_cursor;
      if (d.server) Object.assign(state.server.server, d.server);
      if (Array.isArray(d.rooms)) {
        const byId = new Map(d.rooms.map((r) => [r.room_id, r]));
        for (const rm of state.server.rooms) {
          const patch = byId.get(rm.room_id);
          if (patch) Object.assign(rm, patch);
        }
      }
      renderHeaderAndOverview();
      renderRoomsTab({ preserveDetailEdit: true });
    });

    es.addEventListener("state.patch", (ev) => {
      state.sseConnected = true;
      const d = safeParse(ev.data);
      if (!d || !state.server) return;
      if (d.event_cursor !== undefined) state.server.event_cursor = d.event_cursor;
      if (d.config_revision !== undefined) state.server.config_revision = d.config_revision;
      if (d.revision !== undefined) state.server.revision = d.revision;
      for (const k of ["server", "capacity", "rooms", "bots", "maps", "replays", "logs", "audit"]) {
        if (d[k] !== undefined) {
          if (k === "server" || k === "capacity") {
            Object.assign(state.server[k], d[k]);
          } else {
            state.server[k] = d[k];
          }
        }
      }
      if (!state.capDirty) {
        syncCapDraftFromServer();
      }
      pruneCleanRoomDrafts();
      renderAll({ preserveDetailEdit: true });
    });

    es.addEventListener("command.applied", (ev) => {
      const d = safeParse(ev.data);
      if (!d) return;
      const own = state.pendingCommands.get(d.command_id);
      state.pendingCommands.delete(d.command_id);

      if (state.server && d.event_cursor !== undefined) {
        state.server.event_cursor = d.event_cursor;
      }

      if (own) {
        const restartNote = d.requires_restart ? "（部分监听项需重启专服后生效）" : "";
        showCommandBanner("applied", `命令 ${d.command_id} 已生效（目标修订 v${d.revision}）${restartNote}：${d.summary || d.action}`);
        if (own.action === "set_capacity") {
          state.capDirty = false;
        }
        if (typeof own.target === "string" && own.target.startsWith("room:")) {
          state.roomDrafts.delete(own.target.slice(5));
        }
      } else {
        const note = `另一管理员已完成 ${d.action}（${d.target} · v${d.revision}）`;
        if (state.capDirty || hasAnyDirtyRoomDraft()) {
          showCommandBanner("pending", `${note}；已保留你未提交的本地草稿与起始修订。`);
        } else {
          showCommandBanner("applied", note);
        }
      }
      refreshFullState({ resetDraft: !state.capDirty });
    });

    es.addEventListener("command.failed", (ev) => {
      const d = safeParse(ev.data);
      if (!d) return;
      const own = state.pendingCommands.get(d.command_id);
      state.pendingCommands.delete(d.command_id);
      if (state.server && d.event_cursor !== undefined) {
        state.server.event_cursor = d.event_cursor;
      }
      if (own) {
        showCommandBanner("failed", `命令 ${d.command_id} 执行失败（目标修订仍为 v${d.revision}）：${d.reason || "未知原因"}`);
      }
      renderHeaderAndOverview();
    });

    es.addEventListener("resync_required", () => {
      refreshFullState({ resetDraft: !state.capDirty });
    });

    es.onerror = async () => {
      state.sseConnected = false;
      renderSSEStatus();
      // EventSource uses the same error for a revoked cookie and a lost connection.
      // Preserve the editor on network loss; discard its privileged data on confirmed logout.
      try {
        const check = await apiFetch("/api/v1/session");
        if (state.es !== es || !check.ok || check.data?.authenticated !== false) return;
        clearPrivateView();
        state.session = check.data;
        showGate(check.data.bootstrap_required ? "bootstrap" : "login");
      } catch { /* A temporary disconnect leaves the current administrator's draft intact. */ }
    };
  }

  function hasAnyDirtyRoomDraft() {
    for (const d of state.roomDrafts.values()) {
      if (d.dirty) return true;
    }
    return false;
  }

  function closeSSE() {
    if (state.es) {
      state.es.close();
      state.es = null;
    }
    state.sseConnected = false;
  }

  function safeParse(raw) {
    try {
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  // ---------- Command Submission & 409 Diff ----------
  async function submitCommand(action, target, payload, opts = {}) {
    if (!state.server) return;
    const expectedRevision = opts.expectedRevision !== undefined
      ? opts.expectedRevision
      : getResourceRevision(target);

    const res = await apiFetch("/api/v1/commands", {
      method: "POST",
      json: {
        action,
        target,
        expected_revision: expectedRevision,
        payload,
      },
    });

    if (res.status === 202 && res.data) {
      const cmdId = res.data.command_id;
      state.pendingCommands.set(cmdId, { action, target, expectedRevision });
      showCommandBanner("pending", `已受理请求 ${cmdId}（目标 ${target} · 基于 v${expectedRevision}），等待服务端确认应用…`);
      return { accepted: true, command_id: cmdId };
    }

    if (res.status === 409 && res.data) {
      await handleRevisionConflict(action, target, payload, expectedRevision, res.data);
      return { conflict: true };
    }

    if (res.status === 401) {
      await loadSession();
      return { unauthorized: true };
    }

    const msg = (res.data && res.data.message) || `请求被拒绝 (HTTP ${res.status})`;
    showCommandBanner("failed", msg);
    return { error: msg };
  }

  async function handleRevisionConflict(action, target, payload, expectedRevision, conflictData) {
    const latestRes = await apiFetch("/api/v1/state");
    if (latestRes.ok && latestRes.data) {
      state.server = latestRes.data;
      renderAll({ preserveDetailEdit: true });
    }
    const latestRev = getResourceRevision(target) || conflictData.current_revision;
    const tbody = $("conflict-diff-tbody");
    clearNode(tbody);

    if (action === "set_capacity") {
      $("conflict-title").textContent = "全局容量配置冲突（409 Conflict）";
      $("conflict-desc").textContent = "另一位管理员已更新全局配置版本。你的本地容量草稿已被安全保留，请逐项对比下表差异后决定重提或同步：";
      $("conflict-revs").textContent = `草稿预期 v${expectedRevision} · 服务端最新 v${latestRev}`;

      const serverFlat = {
        ...(state.server.capacity || {}),
        egress_budget_kbps: state.server.server.egress_budget_kbps,
        allow_room_creation: state.server.server.allow_room_creation,
      };
      for (const m of CAP_FIELD_META) {
        appendConflictRow(tbody, m.label, m.key, formatConfigValue(m.key, serverFlat[m.key]), formatConfigValue(m.key, payload[m.key]));
      }
      state.conflictContext = { kind: "capacity", action, target, payload, latestRev };
      $("conflict-modal").classList.remove("hidden");
      return;
    }

    if (typeof target === "string" && target.startsWith("room:")) {
      const rid = target.slice(5);
      const room = findRoom(rid);
      $("conflict-title").textContent = `房间 ${rid} 配置冲突（409 Conflict）`;
      $("conflict-desc").textContent = "另一位值班员或房主操作已更新该房配置修订。你的房间草稿已保留，请对比服务端最新值后确认下一步：";
      $("conflict-revs").textContent = `草稿预期 v${expectedRevision} · 房间最新 v${latestRev}`;

      if (!room) {
        appendConflictRow(tbody, "房间状态", rid, "房间已关闭或已回收", "待提交操作");
      } else if (action === "set_room_map") {
        appendConflictRow(tbody, "下局地图 (next_map_id)", "next_map_id", room.next_map_id || room.map_id || "—", payload.next_map_id);
      } else if (action === "set_room_spectator_policy") {
        appendConflictRow(tbody, "单房观众上限", "spectator_limit", formatSpectatorLimit(room.effective_spectator_limit), formatSpectatorLimit(payload.spectator_limit));
        appendConflictRow(tbody, "观战下发频率", "spectator_snapshot_hz", `${room.spectator_snapshot_hz ?? 10} Hz`, `${payload.spectator_snapshot_hz} Hz`);
      } else if (action === "kick_member") {
        const stillIn = (room.members || []).some((m) => m.member_id === payload.member_id);
        appendConflictRow(tbody, "目标成员状态", payload.member_id, stillIn ? "仍在房间内" : "已离开房间", "移除成员");
      }
      state.conflictContext = { kind: "room", roomId: rid, action, target, payload, latestRev };
      $("conflict-modal").classList.remove("hidden");
      return;
    }

    showCommandBanner("failed", `操作冲突（409）：预期版本 v${expectedRevision} 已过期，当前最新为 v${latestRev}，请确认最新状态后重试。`);
  }

  function appendConflictRow(tbody, label, key, serverText, draftText) {
    const changed = String(serverText) !== String(draftText);
    tbody.appendChild(el("tr", { class: changed ? "conflict-row-diff" : "" }, [
      el("td", {}, [el("strong", { text: label }), el("div", { class: "field-hint mono-num", text: key })]),
      el("td", { class: "mono-num", text: String(serverText) }),
      el("td", { class: "mono-num" }, [
        el("span", {
          class: changed ? "pill pill-warn" : "pill pill-muted",
          text: String(draftText),
        }),
      ]),
    ]));
  }

  function showCommandBanner(kind, text) {
    const banner = $("command-banner");
    banner.className = `command-banner ${kind}`;
    clearNode(banner);
    banner.appendChild(el("span", { text }));
    banner.appendChild(el("button", {
      class: "btn-chip",
      type: "button",
      text: "收起",
      onClick: () => banner.classList.add("hidden"),
    }));
    if (state.bannerTimer) clearTimeout(state.bannerTimer);
    if (kind === "applied") {
      state.bannerTimer = setTimeout(() => banner.classList.add("hidden"), 7000);
    }
  }

  // ---------- Rendering ----------
  function renderAll(opts = {}) {
    if (!state.server) return;
    renderSSEStatus();
    renderHeaderAndOverview();
    renderRoomsTab(opts);
    renderBotsAndMapsTab();
    renderCapacityTab();
    renderReplaysAndAuditTab();
  }

  function renderSSEStatus() {
    const pill = $("sse-pill");
    const txt = $("sse-text");
    const btn = $("btn-resync");
    const cursor = state.server ? state.server.event_cursor || 0 : 0;
    $("hdr-cursor").textContent = `#${cursor}`;
    if (state.sseConnected) {
      pill.className = "sse-pill sse-ok";
      txt.textContent = "实时同步中";
      btn.classList.add("hidden");
    } else {
      pill.className = "sse-pill sse-stale";
      txt.textContent = "连接已断开 · 数据已过期";
      btn.classList.remove("hidden");
    }
  }

  function renderHeaderAndOverview() {
    const d = state.server;
    if (!d) return;
    const srv = d.server || {};
    const cap = d.capacity || {};
    const rooms = d.rooms || [];
    const globalRev = getGlobalRevision();

    $("hdr-server-name").textContent = srv.name || "专服";
    $("hdr-advertise-udp").textContent = srv.advertise_udp || srv.listen_udp || "—";
    $("hdr-fingerprint").textContent = srv.tls_fingerprint || "—";
    $("hdr-cursor").textContent = `#${d.event_cursor || 0}`;
    $("hdr-drain-pill").classList.toggle("hidden", !srv.draining);
    $("hdr-creation-pill").classList.toggle("hidden", Boolean(srv.allow_room_creation));
    $("overview-platform").textContent = srv.platform || "server";

    // Alerts
    const alertStack = $("overview-alerts");
    clearNode(alertStack);
    if (srv.draining) {
      alertStack.appendChild(el("div", { class: "alert-item" }, [
        el("span", { text: `维护排空进行中：已拒绝新建房间与新开赛，当前仍有 ${cap.active_matches ?? 0} 场活跃对局。` }),
        el("button", { class: "btn btn-soft btn-sm", type: "button", text: "前往维护面板", onClick: () => switchTab("capacity") }),
      ]));
    }
    const budget = Number(srv.egress_budget_kbps || 0);
    const egress = Number(srv.egress_kbps || 0);
    const egressRatio = budget > 0 ? (egress / budget) : 0;
    if (budget > 0 && egressRatio >= 0.8) {
      alertStack.appendChild(el("div", { class: "alert-item alert-danger" }, [
        el("span", { text: `出站上行已达预算 ${(egressRatio * 100).toFixed(0)}%（${egress.toFixed(1)} / ${budget} kbps），建议优先调低高观众房间的观战频率。` }),
        el("button", { class: "btn btn-soft btn-sm", type: "button", text: "排查高负载房间", onClick: () => switchTab("rooms") }),
      ]));
    }
    const faultBots = (d.bots || []).filter((b) => (b.timeout_count || 0) > 0 || b.last_error);
    if (faultBots.length) {
      alertStack.appendChild(el("div", { class: "alert-item" }, [
        el("span", { text: `发现 ${faultBots.length} 名 AI 选手有超时/故障记录（例如 ${faultBots[0].label}：${faultBots[0].last_error || "超时 " + faultBots[0].timeout_count + " 次"}）。` }),
        el("button", { class: "btn btn-soft btn-sm", type: "button", text: "查看选手诊断", onClick: () => switchTab("bots-maps") }),
      ]));
    }

    // KPIs
    let totalHumans = 0;
    let totalAI = 0;
    let totalSpecs = 0;
    for (const r of rooms) {
      totalHumans += r.human_count || 0;
      totalAI += r.ai_count || 0;
      totalSpecs += r.spectator_count || 0;
    }

    $("kpi-matches").textContent = `${cap.active_matches ?? 0} / ${cap.max_active_matches ?? 0}`;
    $("kpi-matches-sub").textContent = `开放房间 ${cap.open_rooms ?? rooms.length} / ${cap.max_rooms ?? 0}`;

    $("kpi-population").textContent = String(totalHumans + totalAI + totalSpecs);
    $("kpi-population-sub").textContent = `真人 ${totalHumans} · AI ${totalAI} · 观众 ${totalSpecs}`;

    const rssStr = srv.rss_mib !== undefined ? `${Number(srv.rss_mib).toFixed(1)} MiB` : "—";
    if (srv.cpu_percent !== undefined && srv.cpu_percent !== null) {
      $("kpi-compute").textContent = `${Number(srv.cpu_percent).toFixed(1)}%`;
      $("kpi-compute-sub").textContent = `宿主 RSS ${rssStr} · 选手 RSS ${Number(srv.bot_rss_mib ?? 0).toFixed(1)} MiB`;
    } else {
      $("kpi-compute").textContent = rssStr;
      $("kpi-compute-sub").textContent = "进程常驻内存 RSS（CPU 采样待接）";
    }

    $("kpi-egress").textContent = `${egress.toFixed(1)} kbps`;
    const bar = $("kpi-egress-bar");
    if (budget > 0) {
      $("kpi-egress-sub").textContent = `预算 ${budget} kbps（${(egressRatio * 100).toFixed(1)}%）`;
      bar.style.width = `${Math.min(100, egressRatio * 100).toFixed(1)}%`;
      bar.className = `progress-fill${egressRatio >= 0.9 ? " danger" : egressRatio >= 0.75 ? " warn" : ""}`;
    } else {
      $("kpi-egress-sub").textContent = "预算 0 kbps（不限人工预算）";
      bar.style.width = "0%";
      bar.className = "progress-fill";
    }

    if (srv.tick_late_p99_ms !== undefined && srv.tick_late_p99_ms !== null) {
      $("kpi-ticklate").textContent = `${Number(srv.tick_late_p99_ms).toFixed(1)} ms`;
    } else {
      $("kpi-ticklate").textContent = "—";
    }
    $("kpi-revisions").textContent = `v${globalRev}`;
    $("kpi-revisions-sub").textContent = `SSE event_cursor #${d.event_cursor || 0}`;

    // Overview room table
    const tbody = $("overview-room-tbody");
    clearNode(tbody);
    if (!rooms.length) {
      tbody.appendChild(el("tr", {}, [el("td", { colspan: "8", text: "当前暂无活跃或等待中的房间。" })]));
      return;
    }
    for (const r of rooms) {
      const tr = el("tr", {
        class: "clickable-row",
        onClick: () => {
          state.selectedRoomId = r.room_id;
          state.mobileDetailOpen = true;
          switchTab("rooms");
          renderRoomsTab();
        },
      }, [
        el("td", {}, [el("strong", { text: r.name || r.title || r.room_id }), el("div", { class: "field-hint mono-num", text: `${r.room_id} · ${MODE_LABELS[r.mode] || r.mode}` })]),
        el("td", {}, [buildRoomStatePill(r.state)]),
        el("td", { class: "mono-num", text: `${r.human_count ?? 0} 真人 / ${r.ai_count ?? 0} AI` }),
        el("td", {}, [buildSpectatorPill(r)]),
        el("td", { class: "mono-num", text: `${r.effective_spectator_hz ?? r.spectator_snapshot_hz ?? 10} Hz` }),
        el("td", { class: "mono-num", text: r.tick_tps == null ? "—" : `${Number(r.tick_tps).toFixed(1)} TPS` }),
        el("td", { class: "mono-num", text: `${Number(r.egress_kbps || 0).toFixed(1)} kbps` }),
        el("td", { class: "mono-num", text: String(r.snapshot_backlog || 0) }),
      ]);
      tbody.appendChild(tr);
    }
  }

  function buildRoomStatePill(st) {
    const cls = st === "playing"
      ? "pill-success"
      : st === "paused"
        ? "pill-warn"
        : (st === "loading" || st === "countdown" || st === "settling")
          ? "pill-info"
          : "pill-muted";
    return el("span", { class: `pill ${cls}`, text: ROOM_STATE_LABELS[st] || st });
  }

  function buildSpectatorPill(room) {
    const cnt = room.spectator_count ?? 0;
    const lim = room.effective_spectator_limit;
    if (lim === -1) {
      return el("span", { class: "pill pill-muted mono-num", text: `${cnt} · 不限` });
    }
    if (room.spectator_join_block_reason === "spectator_limit_reached") {
      return el("span", {
        class: "pill pill-warn mono-num",
        title: "上限已低于或等于当前在线数：新观众会被拒绝，现存观众保留不断线",
        text: `${cnt} / 上限 ${lim} · 拒新保留存量`,
      });
    }
    if (lim === 0) {
      return el("span", { class: "pill pill-muted mono-num", text: `${cnt} / 禁入 (0)` });
    }
    return el("span", { class: "pill pill-success mono-num", text: `${cnt} / 上限 ${lim}` });
  }

  function matchStateFilter(roomState, filterState) {
    if (filterState === "all") return true;
    if (filterState === "ready") return roomState === "ready" || roomState === "lobby";
    if (filterState === "loading") return roomState === "loading" || roomState === "countdown";
    if (filterState === "finished") return roomState === "finished" || roomState === "settling";
    return roomState === filterState;
  }

  // ---------- Rooms Tab ----------
  function renderRoomsTab(opts = {}) {
    if (!state.server) return;
    const toggleBtn = $("btn-toggle-creation");
    const allowCreate = Boolean(state.server.server && state.server.server.allow_room_creation);
    toggleBtn.textContent = allowCreate ? "暂停专服新建房间" : "恢复专服新建房间";
    toggleBtn.disabled = !canOperate();
    toggleBtn.title = canOperate() ? "" : "仅值班员或管理员可调整";

    const allRooms = state.server.rooms || [];
    const f = state.filters;
    const q = f.search.trim().toLowerCase();
    const rooms = allRooms.filter((r) => {
      if (f.mode !== "all" && r.mode !== f.mode) return false;
      if (!matchStateFilter(r.state, f.state)) return false;
      if (f.lock === "locked" && !r.has_password) return false;
      if (f.lock === "public" && r.has_password) return false;
      if (q && !`${r.room_id} ${r.name || ""} ${r.title || ""} ${r.map_id || r.map || ""}`.toLowerCase().includes(q)) return false;
      return true;
    });

    $("rooms-count-pill").textContent = `${rooms.length} / ${allRooms.length} 间`;

    const list = $("rooms-list");
    const prevScrollTop = list.scrollTop;
    clearNode(list);
    if (!rooms.length) {
      list.appendChild(el("div", { class: "panel", text: "没有符合当前筛选条件的房间。" }));
    } else {
      for (const r of rooms) {
        const draft = state.roomDrafts.get(r.room_id);
        const curMap = r.map_id || r.map || "—";
        const card = el("article", {
          class: `room-card${r.room_id === state.selectedRoomId ? " selected" : ""}`,
          onClick: () => {
            state.selectedRoomId = r.room_id;
            state.mobileDetailOpen = true;
            state.confirmKey = null;
            renderRoomsTab();
          },
        }, [
          el("div", { class: "room-card-top" }, [
            el("span", { class: "room-card-title", text: r.name || r.title || r.room_id }),
            buildRoomStatePill(r.state),
          ]),
          el("div", { class: "room-card-meta mono-num" }, [
            el("span", { text: `${r.room_id} (v${getRoomRevision(r)})` }),
            el("span", { text: MODE_LABELS[r.mode] || r.mode }),
            el("span", { text: `地图：${curMap}${r.next_map_id ? " → " + r.next_map_id : ""}` }),
            el("span", { text: `${Number(r.egress_kbps || 0).toFixed(1)} kbps · ${r.tick_tps == null ? "等待开赛" : Number(r.tick_tps).toFixed(1) + " TPS"}` }),
          ]),
          el("div", { class: "room-card-badges" }, [
            el("span", { class: `pill ${r.has_password ? "pill-info" : "pill-muted"}`, text: r.has_password ? "密码房" : "公开房" }),
            el("span", { class: "pill pill-muted mono-num", text: `${r.human_count ?? 0} 真人 · ${r.ai_count ?? 0} AI` }),
            buildSpectatorPill(r),
            draft && draft.dirty ? el("span", { class: "pill pill-warn", text: `本地草稿未提交 (v${draft.startRev})` }) : null,
          ]),
        ]);
        list.appendChild(card);
      }
    }
    list.scrollTop = prevScrollTop;

    const detailBox = $("room-detail");
    detailBox.classList.toggle("mobile-open", Boolean(state.mobileDetailOpen));
    const selectedRoom = findRoom(state.selectedRoomId) || rooms[0] || null;
    const selectedDraft = selectedRoom && state.roomDrafts.get(selectedRoom.room_id);
    const detailEditing = detailBox && (detailBox.contains(document.activeElement) || (selectedDraft && selectedDraft.dirty) || (state.confirmKey && state.confirmKey.startsWith("kick:")));
    if (opts.preserveDetailEdit && detailEditing) {
      return;
    }
    renderRoomDetail(selectedRoom);
  }

  function renderRoomDetail(room) {
    const box = $("room-detail");
    clearNode(box);

    // Mobile drawer sticky return bar (hidden on desktop >= 860px)
    box.appendChild(el("div", { class: "mobile-detail-bar" }, [
      el("strong", { text: room ? `${room.name || room.title || room.room_id} · 调控详情` : "房间详情" }),
      el("button", {
        id: "btn-mobile-detail-close",
        class: "btn btn-soft btn-sm",
        type: "button",
        text: "✕ 返回房间列表",
        onClick: () => {
          state.mobileDetailOpen = false;
          box.classList.remove("mobile-open");
        },
      }),
    ]));

    if (!room) {
      box.appendChild(el("p", { class: "panel-sub", text: "请从左侧选择一个房间查看成员延迟、公开票案及调控参数。" }));
      return;
    }

    const op = canOperate();
    const roomRev = getRoomRevision(room);
    const draft = getRoomDraft(room);
    const maps = (state.server.maps || []).filter((m) => m.enabled);

    const markRoomDirty = () => {
      if (!draft.dirty) {
        draft.startRev = getRoomRevision(room);
        draft.dirty = true;
      }
      state.roomDrafts.set(room.room_id, draft);
    };

    box.appendChild(el("div", { class: "panel-head" }, [
      el("div", {}, [
        el("h2", { text: `${room.name || room.title || room.room_id} (${room.room_id})` }),
        el("p", {
          class: "panel-sub mono-num",
          text: `房间配置修订 v${roomRev} · room_rev=${room.room_rev ?? 0} · round_epoch=${room.round_epoch ?? 0} · 策略 ${room.map_strategy || "specified"}`,
        }),
      ]),
      buildRoomStatePill(room.state),
    ]));

    if (draft.dirty && roomRev !== draft.startRev) {
      box.appendChild(el("div", { class: "alert-item", style: "margin-bottom: 12px;" }, [
        el("span", { text: `房间修订已推进至 v${roomRev}，你的本地草稿仍锁定起始 v${draft.startRev}；提交将触发 409 差异对比以防静默覆盖。` }),
        el("button", {
          class: "btn btn-soft btn-sm",
          type: "button",
          text: "放弃草稿",
          onClick: () => {
            state.roomDrafts.delete(room.room_id);
            renderRoomDetail(room);
          },
        }),
      ]));
    }

    if (room.spectator_join_block_reason === "spectator_limit_reached") {
      box.appendChild(el("div", { class: "alert-item", style: "margin-bottom: 12px;" }, [
        el("span", { text: `观战限额说明：当前生效上限 ${room.effective_spectator_limit}，在线观众 ${room.spectator_count} 人。新观众接入将被拒绝（spectator_limit_reached），存量 ${room.spectator_count} 位观众保持不断线。` }),
      ]));
    }

    // Room controls (operator+)
    const mapSelect = el("select", {
      id: "detail-next-map",
      disabled: !op,
      onChange: (e) => {
        markRoomDirty();
        draft.next_map_id = e.target.value;
      },
    });
    for (const m of maps) {
      const opt = el("option", { value: m.id, text: `${m.title || m.name || m.id} (${m.id})` });
      if (draft.next_map_id === m.id) opt.selected = true;
      mapSelect.appendChild(opt);
    }

    const specLimInput = el("input", {
      id: "detail-spec-limit",
      type: "number",
      min: "-1",
      max: "10000",
      value: String(draft.spectator_limit),
      disabled: !op,
      style: "width: 110px;",
      onInput: (e) => {
        markRoomDirty();
        draft.spectator_limit = Number(e.target.value);
      },
    });

    const hzOptions = Array.from(new Set([5, 10, 20, Number(draft.spectator_snapshot_hz || 10)])).sort((a, b) => a - b);
    const specHzSelect = el("select", {
      id: "detail-spec-hz",
      disabled: !op,
      onChange: (e) => {
        markRoomDirty();
        draft.spectator_snapshot_hz = Number(e.target.value);
      },
    });
    for (const hz of hzOptions) {
      const opt = el("option", { value: String(hz), text: `${hz} Hz` });
      if (Number(draft.spectator_snapshot_hz) === hz) opt.selected = true;
      specHzSelect.appendChild(opt);
    }

    box.appendChild(el("div", { class: "detail-section" }, [
      el("h3", { text: `单房调控（锁定预期房间修订 v${draft.startRev}，不改变比赛速度）` }),
      el("div", { class: "inline-controls" }, [
        el("div", { class: "field" }, [el("label", { for: "detail-next-map", text: "下局地图" }), mapSelect]),
        el("button", {
          id: "btn-room-set-map",
          class: "btn btn-soft btn-sm",
          type: "button",
          disabled: !op,
          text: "设定下局地图",
          onClick: () => submitCommand("set_room_map", `room:${room.room_id}`, { next_map_id: draft.next_map_id }, { expectedRevision: draft.startRev }),
        }),
        el("div", { class: "field" }, [el("label", { for: "detail-spec-limit", text: "单房观众上限 (-1 不限)" }), specLimInput]),
        el("div", { class: "field" }, [el("label", { for: "detail-spec-hz", text: "观战频率" }), specHzSelect]),
        el("button", {
          id: "btn-room-set-spec",
          class: "btn btn-primary btn-sm",
          type: "button",
          disabled: !op,
          text: "应用观战策略",
          onClick: () => submitCommand("set_room_spectator_policy", `room:${room.room_id}`, {
            spectator_limit: Number(draft.spectator_limit),
            spectator_snapshot_hz: Number(draft.spectator_snapshot_hz),
          }, { expectedRevision: draft.startRev }),
        }),
      ]),
    ]));

    // Pause vote read-only status (supports both room.cpp cast/needed and mock proposer/yes_count)
    const voteBox = el("div", { class: "detail-section" }, [el("h3", { text: "公开暂停票案（管理台只读，严禁代投或调速）" })]);
    if (room.pause_vote) {
      const pv = room.pause_vote;
      if (pv.cast !== undefined) {
        const yes = Object.values(pv.cast || {}).filter(Boolean).length;
        voteBox.appendChild(el("p", {
          class: "panel-sub mono-num",
          text: `已投赞成 ${yes} / 需过半 ${pv.needed ?? "?"} 票`,
        }));
      } else {
        voteBox.appendChild(el("p", {
          class: "panel-sub",
          text: `发起人：${pv.proposer || "—"} · 赞成 ${pv.yes_count ?? 0} / 需过半 ${pv.required ?? 1} · 剩余 ${pv.seconds_left ?? 0} s`,
        }));
      }
    } else {
      voteBox.appendChild(el("p", { class: "panel-sub", text: "当前无进行中的暂停或恢复表决。" }));
    }
    box.appendChild(voteBox);

    // Members table with non-blocking 2-step inline confirm
    const memWrap = el("div", { class: "detail-section" }, [el("h3", { text: "房间成员、观众与网络状态" })]);
    const tbl = el("table", { class: "data-table" }, [
      el("thead", {}, [el("tr", {}, [
        el("th", { text: "席位" }),
        el("th", { text: "成员 / 选手" }),
        el("th", { text: "类型" }),
        el("th", { text: "RTT / 丢包" }),
        el("th", { text: "积压" }),
        el("th", { text: "操作" }),
      ])]),
    ]);
    const tbody = el("tbody");
    const members = room.members || [];
    if (!members.length) {
      tbody.appendChild(el("tr", {}, [el("td", { colspan: "6", text: "当前房间暂无成员。" })]));
    } else {
      for (const m of members) {
        const seatLabel = m.seat !== undefined && m.seat >= 0 ? `#${m.seat}` : "观战";
        const netText = m.kind === "ai"
          ? "本机沙箱"
          : (m.rtt_ms != null
            ? `${Math.round(m.rtt_ms)} ms${m.packet_loss_pct != null ? " / " + m.packet_loss_pct + "%" : ""}`
            : "—");
        const kickKey = `kick:${room.room_id}:${m.member_id}`;
        const actionCell = state.confirmKey === kickKey
          ? el("div", { class: "confirm-inline" }, [
            el("button", {
              class: "btn btn-danger btn-sm",
              type: "button",
              disabled: !op,
              text: "确认移除",
              onClick: () => {
                state.confirmKey = null;
                submitCommand("kick_member", `room:${room.room_id}`, { member_id: m.member_id }, { expectedRevision: getRoomRevision(room) });
              },
            }),
            el("button", {
              class: "btn btn-soft btn-sm",
              type: "button",
              text: "取消",
              onClick: () => {
                state.confirmKey = null;
                renderRoomDetail(room);
              },
            }),
          ])
          : el("button", {
            class: "btn btn-danger btn-sm",
            type: "button",
            disabled: !op,
            text: "移除",
            onClick: () => {
              state.confirmKey = kickKey;
              renderRoomDetail(room);
            },
          });

        tbody.appendChild(el("tr", {}, [
          el("td", { class: "mono-num", text: seatLabel }),
          el("td", {}, [
            el("strong", { text: m.name || m.member_id }),
            m.disconnected ? el("span", { class: "pill pill-danger", style: "margin-left:6px;", text: "已掉线" }) : null,
          ]),
          el("td", { text: m.kind === "ai" ? "AI 进程" : "真人" }),
          el("td", { class: "mono-num", text: netText }),
          el("td", { class: "mono-num", text: String(m.backlog || 0) }),
          el("td", {}, [actionCell]),
        ]));
      }
    }
    tbl.appendChild(tbody);
    memWrap.appendChild(el("div", { class: "table-wrap" }, [tbl]));
    box.appendChild(memWrap);
  }

  // ---------- Bots & Maps Tab ----------
  function renderBotsAndMapsTab() {
    if (!state.server) return;
    const op = canOperate();
    const adm = canAdmin();
    const globalRev = getGlobalRevision();

    const botsBody = $("bots-tbody");
    clearNode(botsBody);
    for (const b of state.server.bots || []) {
      const stepText = b.first_step_ms != null && b.avg_step_ms != null
        ? `${b.first_step_ms} / ${b.avg_step_ms} ms`
        : "待局内采样";
      const timeouts = b.timeout_count ?? 0;
      botsBody.appendChild(el("tr", {}, [
        el("td", {}, [el("strong", { text: b.label || b.name || b.key }), el("div", { class: "field-hint mono-num", text: b.key || b.id })]),
        el("td", { text: b.source === "official" ? "官方" : "社区审核" }),
        el("td", {}, [
          el("span", { class: `pill ${b.binary_ready ? "pill-success" : "pill-danger"}`, text: b.binary_ready ? "就绪" : "缺失" }),
          el("div", { class: "field-hint mono-num", text: b.sandbox_status || (b.binary_ready ? "沙箱可用" : "缺少可执行文件") }),
        ]),
        el("td", { class: "mono-num", text: stepText }),
        el("td", { class: "mono-num" }, [
          el("span", { class: `pill ${timeouts > 0 ? "pill-warn" : "pill-muted"}`, text: String(timeouts) }),
        ]),
        el("td", { class: "mono-num", text: `${b.active_rooms ?? 0} 房` }),
        el("td", { text: b.last_error || "正常" }),
        el("td", {}, [
          el("button", {
            class: `btn btn-sm ${b.enabled ? "btn-soft" : "btn-primary"}`,
            type: "button",
            disabled: !op,
            text: b.enabled ? "禁用下局选取" : "重新启用",
            onClick: () => submitCommand("set_bot_enabled", `bot:${b.key || b.id}`, { enabled: !b.enabled }, { expectedRevision: globalRev }),
          }),
        ]),
      ]));
    }

    const mapsBody = $("maps-tbody");
    clearNode(mapsBody);
    for (const m of state.server.maps || []) {
      const modes = (m.modes || []).join(" / ");
      mapsBody.appendChild(el("tr", {}, [
        el("td", {}, [el("strong", { text: m.title || m.name || m.id })]),
        el("td", { class: "mono-num", text: m.id }),
        el("td", { text: m.theme || "—" }),
        el("td", { class: "mono-num", text: modes }),
        el("td", {}, [
          (m.modes || []).includes("ffa3")
            ? el("span", { class: `pill ${m.trio_verified ? "pill-success" : "pill-warn"}`, text: m.trio_verified ? "三席平衡通过" : "待验" })
            : el("span", { class: "pill pill-muted", text: "不用于 3 人" }),
        ]),
        el("td", {}, [
          el("button", {
            class: `btn btn-sm ${m.enabled ? "btn-soft" : "btn-primary"}`,
            type: "button",
            disabled: !adm,
            text: m.enabled ? "下线地图" : "上线地图",
            onClick: () => submitCommand("set_map_enabled", `map:${m.id}`, { enabled: !m.enabled }, { expectedRevision: globalRev }),
          }),
        ]),
      ]));
    }
  }

  // ---------- Capacity & Config Tab ----------
  function populateCapacityInputs() {
    if (!state.capDraft) return;
    $("cap-max-rooms").value = String(state.capDraft.max_rooms);
    $("cap-max-matches").value = String(state.capDraft.max_active_matches);
    $("cap-max-specs").value = String(state.capDraft.max_spectators_per_room);
    const hzSel = $("cap-spec-hz");
    const targetHz = String(state.capDraft.spectator_snapshot_hz);
    if (!Array.from(hzSel.options).some((o) => o.value === targetHz)) {
      hzSel.appendChild(el("option", { value: targetHz, text: `${targetHz} Hz` }));
    }
    hzSel.value = targetHz;
    $("cap-egress-budget").value = String(state.capDraft.egress_budget_kbps);
    $("cap-idle-ttl").value = String(state.capDraft.idle_room_ttl_seconds);
    $("cap-allow-create").checked = Boolean(state.capDraft.allow_room_creation);
    renderCapacityDiffPreview();
  }

  function readCapacityInputs() {
    if (!state.capDirty) {
      state.capDraftRev = getGlobalRevision();
      state.capDirty = true;
    }
    state.capDraft = {
      max_rooms: Number($("cap-max-rooms").value),
      max_active_matches: Number($("cap-max-matches").value),
      max_spectators_per_room: Number($("cap-max-specs").value),
      spectator_snapshot_hz: Number($("cap-spec-hz").value),
      egress_budget_kbps: Number($("cap-egress-budget").value),
      idle_room_ttl_seconds: Number($("cap-idle-ttl").value),
      allow_room_creation: Boolean($("cap-allow-create").checked),
    };
    renderCapacityDiffPreview();
  }

  function formatSpectatorLimit(val) {
    const n = Number(val);
    if (n === -1) return "-1 (无上限)";
    if (n === 0) return "0 (禁入)";
    return String(val);
  }

  function formatConfigValue(key, val) {
    if (key === "allow_room_creation") return val ? "允许新建" : "禁止新建";
    if (key === "max_spectators_per_room") return formatSpectatorLimit(val);
    if (key === "egress_budget_kbps" && Number(val) === 0) return "0 (不限预算)";
    return String(val);
  }

  function renderCapacityDiffPreview() {
    if (!state.server || !state.capDraft) return;
    const srv = state.server.server || {};
    const cap = state.server.capacity || {};
    const serverRev = getGlobalRevision();
    const current = {
      max_rooms: cap.max_rooms,
      max_active_matches: cap.max_active_matches,
      max_spectators_per_room: cap.max_spectators_per_room,
      spectator_snapshot_hz: cap.spectator_snapshot_hz,
      egress_budget_kbps: srv.egress_budget_kbps !== undefined ? srv.egress_budget_kbps : 0,
      idle_room_ttl_seconds: cap.idle_room_ttl_seconds !== undefined ? cap.idle_room_ttl_seconds : 300,
      allow_room_creation: Boolean(srv.allow_room_creation),
    };

    const preview = $("cap-diff-preview");
    clearNode(preview);
    const diffs = CAP_FIELD_META.filter((m) => String(current[m.key]) !== String(state.capDraft[m.key]));
    const staleConflict = state.capDirty && serverRev !== state.capDraftRev;
    if (!diffs.length && !staleConflict) {
      preview.classList.add("hidden");
      return;
    }
    preview.classList.remove("hidden");
    preview.appendChild(el("h3", {
      text: `待提交变更预览（${diffs.length} 项改动，草稿锁定起始版本 expected_revision=v${state.capDraftRev}）`,
    }));
    if (staleConflict) {
      preview.appendChild(el("p", {
        class: "panel-sub",
        style: "color: var(--warn-ink); margin-bottom: 6px; font-weight: 600;",
        text: `⚠ 服务端配置已更新为 v${serverRev}，但你的草稿仍锁定起始 v${state.capDraftRev}；提交将触发 409 差异对比，不会悄悄覆盖他人的修改。`,
      }));
    }
    for (const m of diffs) {
      preview.appendChild(el("div", { class: "diff-row" }, [
        el("span", {}, [el("strong", { text: m.label }), el("span", { class: "mono-num", text: `：${formatConfigValue(m.key, current[m.key])} → ${formatConfigValue(m.key, state.capDraft[m.key])}` })]),
        el("span", { class: "pill pill-info", text: m.scope }),
      ]));
    }
  }

  function renderCapacityTab() {
    if (!state.server) return;
    const d = state.server;
    const srv = d.server || {};
    const adm = canAdmin();
    const globalRev = getGlobalRevision();

    $("cap-rev-badge").textContent = state.capDirty && state.capDraftRev !== globalRev
      ? `服务端 v${globalRev} · 草稿基于 v${state.capDraftRev}`
      : `配置修订 v${globalRev}`;

    for (const id of ["cap-max-rooms", "cap-max-matches", "cap-max-specs", "cap-spec-hz", "cap-egress-budget", "cap-idle-ttl", "cap-allow-create", "btn-cap-submit", "btn-cap-reset", "btn-toggle-drain"]) {
      $(id).disabled = !adm;
    }

    $("ops-drain-status").textContent = srv.draining ? "正在维护排空（拒接新房/新开局）" : "正常接单中";
    const drainBtn = $("btn-toggle-drain");
    if (state.confirmKey === "drain") {
      drainBtn.textContent = "确认开启维护排空？（再点一次执行）";
      drainBtn.className = "btn btn-danger";
    } else {
      drainBtn.textContent = srv.draining ? "取消排空，恢复接单" : "开启维护排空";
      drainBtn.className = `btn ${srv.draining ? "btn-primary" : "btn-warn"}`;
    }

    $("ro-listen-udp").textContent = srv.listen_udp || "—";
    $("ro-adv-udp").textContent = srv.advertise_udp || "—";
    $("ro-admin-listen").textContent = srv.admin_listen || "—";
    $("ro-public-origin").textContent = srv.public_origin || "—";

    renderCapacityDiffPreview();
  }

  // ---------- Replays, Logs & Audit Tab ----------
  function renderReplaysAndAuditTab() {
    if (!state.server) return;
    const q = state.replaySearch.trim().toLowerCase();
    const replays = (state.server.replays || []).filter((rp) => {
      if (!q) return true;
      return `${rp.replay_id} ${rp.room_name} ${rp.map_id} ${rp.summary}`.toLowerCase().includes(q);
    });

    const rBody = $("replays-tbody");
    clearNode(rBody);
    if (!replays.length) {
      rBody.appendChild(el("tr", {}, [el("td", { colspan: "7", text: "暂无符合条件的已结算回放。" })]));
    } else {
      for (const rp of replays) {
        rBody.appendChild(el("tr", {}, [
          el("td", { class: "mono-num", text: rp.replay_id }),
          el("td", {}, [el("strong", { text: rp.room_name }), el("div", { class: "field-hint mono-num", text: `${rp.room_id} · 轮次 ${rp.round_epoch}` })]),
          el("td", { text: `${MODE_LABELS[rp.mode] || rp.mode} · ${rp.map_id}` }),
          el("td", { class: "mono-num", text: `${rp.ticks} 步` }),
          el("td", { text: rp.summary }),
          el("td", { class: "mono-num", text: rp.finished_at }),
          el("td", {}, [
            rp.downloadable
              ? el("a", {
                  class: "btn btn-soft btn-sm",
                  href: `/api/v1/replays/${encodeURIComponent(rp.replay_id)}`,
                  download: `${rp.replay_id}.fbar`,
                  text: "下载",
                })
              : el("span", { class: "pill pill-muted", text: "延迟保护中" }),
          ]),
        ]));
      }
    }

    const lq = state.logSearch.trim().toLowerCase();
    const logs = (state.server.logs || []).filter((lg) => {
      if (!lq) return true;
      return `${lg.source || ""} ${lg.room_id || ""} ${lg.message || ""}`.toLowerCase().includes(lq);
    });
    const logsBox = $("logs-list");
    clearNode(logsBox);
    if (!logs.length) {
      logsBox.appendChild(el("div", { class: "feed-item", text: "暂无匹配的脱敏运行或故障日志。" }));
    } else {
      for (const lg of logs) {
        logsBox.appendChild(el("div", { class: "feed-item" }, [
          el("div", { class: "feed-item-head mono-num" }, [
            el("span", { text: `[${(lg.level || "info").toUpperCase()}] ${lg.source || "core"}${lg.room_id ? " · " + lg.room_id : ""}` }),
            el("span", { text: lg.stamp }),
          ]),
          el("div", { text: lg.message }),
        ]));
      }
    }

    const auditBox = $("audit-list");
    clearNode(auditBox);
    if (!(state.server.audit || []).length) {
      auditBox.appendChild(el("div", { class: "feed-item", text: "暂无管理操作审计记录。" }));
    } else {
      for (const au of state.server.audit) {
        auditBox.appendChild(el("div", { class: "feed-item" }, [
          el("div", { class: "feed-item-head mono-num" }, [
            el("span", { text: `${au.actor} (${au.role}) · ${au.action} -> ${au.target}` }),
            el("span", { text: `${au.command_id || ""} · ${au.stamp}` }),
          ]),
          el("div", { text: `[${au.result}] ${au.detail}` }),
        ]));
      }
    }
  }

  // ---------- Event Binding ----------
  function switchTab(tabName) {
    state.activeTab = tabName;
    state.confirmKey = null;
    if (tabName !== "rooms") state.mobileDetailOpen = false;
    for (const btn of document.querySelectorAll(".tab-btn")) {
      const active = btn.dataset.tab === tabName;
      btn.classList.toggle("active", active);
      btn.setAttribute("aria-selected", active ? "true" : "false");
    }
    for (const sec of document.querySelectorAll(".tab-panel")) {
      sec.classList.toggle("active", sec.id === `tab-${tabName}`);
    }
  }

  function bindStaticEvents() {
    $("btn-recheck-session").addEventListener("click", loadSession);
    $("login-form").addEventListener("submit", handleLogin);
    $("btn-logout").addEventListener("click", handleLogout);
    $("btn-resync").addEventListener("click", () => {
      refreshFullState({ resetDraft: !state.capDirty });
      connectSSE();
    });

    $("btn-copy-fp").addEventListener("click", async () => {
      const fp = $("hdr-fingerprint").textContent;
      if (navigator.clipboard && fp && fp !== "—") {
        try {
          await navigator.clipboard.writeText(fp);
          showCommandBanner("applied", "已复制服务端 TLS 公钥指纹到剪贴板。");
        } catch {
          showCommandBanner("failed", "剪贴板访问受限，请手动选中复制。");
        }
      }
    });

    for (const btn of document.querySelectorAll(".tab-btn")) {
      btn.addEventListener("click", () => switchTab(btn.dataset.tab));
    }

    $("filter-mode").addEventListener("change", (e) => { state.filters.mode = e.target.value; renderRoomsTab(); });
    $("filter-state").addEventListener("change", (e) => { state.filters.state = e.target.value; renderRoomsTab(); });
    $("filter-lock").addEventListener("change", (e) => { state.filters.lock = e.target.value; renderRoomsTab(); });
    $("filter-search").addEventListener("input", (e) => { state.filters.search = e.target.value; renderRoomsTab(); });
    $("replay-filter").addEventListener("input", (e) => { state.replaySearch = e.target.value; renderReplaysAndAuditTab(); });
    $("logs-filter").addEventListener("input", (e) => { state.logSearch = e.target.value; renderReplaysAndAuditTab(); });

    $("btn-toggle-creation").addEventListener("click", () => {
      if (!state.server) return;
      const cur = Boolean(state.server.server && state.server.server.allow_room_creation);
      submitCommand("set_room_creation", "server", { allow_room_creation: !cur }, { expectedRevision: getGlobalRevision() });
    });

    for (const id of ["cap-max-rooms", "cap-max-matches", "cap-max-specs", "cap-spec-hz", "cap-egress-budget", "cap-idle-ttl", "cap-allow-create"]) {
      $(id).addEventListener("input", readCapacityInputs);
      $(id).addEventListener("change", readCapacityInputs);
    }

    $("capacity-form").addEventListener("submit", (ev) => {
      ev.preventDefault();
      readCapacityInputs();
      submitCommand("set_capacity", "capacity", { ...state.capDraft }, { expectedRevision: state.capDraftRev });
    });

    $("btn-cap-reset").addEventListener("click", () => {
      syncCapDraftFromServer();
      renderCapacityTab();
      showCommandBanner("applied", "已放弃未提交草稿，还原为服务端当前配置值。");
    });

    $("btn-toggle-drain").addEventListener("click", () => {
      if (!state.server) return;
      const cur = Boolean(state.server.server && state.server.server.draining);
      if (!cur && state.confirmKey !== "drain") {
        state.confirmKey = "drain";
        renderCapacityTab();
        return;
      }
      state.confirmKey = null;
      renderCapacityTab();
      submitCommand("set_drain_mode", "server", { draining: !cur }, { expectedRevision: getGlobalRevision() });
    });

    $("btn-conflict-resubmit").addEventListener("click", async () => {
      const ctx = state.conflictContext;
      $("conflict-modal").classList.add("hidden");
      state.conflictContext = null;
      if (!ctx) return;
      if (ctx.kind === "capacity") {
        state.capDraftRev = ctx.latestRev;
      } else if (ctx.kind === "room" && ctx.roomId) {
        const d = state.roomDrafts.get(ctx.roomId);
        if (d) d.startRev = ctx.latestRev;
      }
      await submitCommand(ctx.action, ctx.target, ctx.payload, { expectedRevision: ctx.latestRev });
    });

    $("btn-conflict-discard").addEventListener("click", () => {
      const ctx = state.conflictContext;
      $("conflict-modal").classList.add("hidden");
      state.conflictContext = null;
      if (ctx && ctx.kind === "room" && ctx.roomId) {
        state.roomDrafts.delete(ctx.roomId);
        renderRoomsTab();
        showCommandBanner("applied", `已放弃房间 ${ctx.roomId} 冲突草稿并同步服务端最新配置。`);
        return;
      }
      syncCapDraftFromServer();
      renderCapacityTab();
      showCommandBanner("applied", "已丢弃冲突草稿并同步服务端最新配置。");
    });
  }

  document.addEventListener("DOMContentLoaded", boot);
})();
