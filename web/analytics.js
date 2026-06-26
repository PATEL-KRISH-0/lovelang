(function () {
  const cfg = window.LOVELANG_TRACKING || {};
  const endpoint = typeof cfg.endpoint === "string" ? cfg.endpoint.trim() : "";
  const page = cfg.page || "unknown";

  if (!endpoint) {
    return;
  }

  const loadCountKey = "lovelang:loadCount:" + page;
  const actionCountKey = "lovelang:actionCount:" + page;

  function safeInt(raw) {
    const value = Number(raw);
    return Number.isFinite(value) && value >= 0 ? Math.floor(value) : 0;
  }

  function nextCount(key) {
    const next = safeInt(localStorage.getItem(key)) + 1;
    localStorage.setItem(key, String(next));
    return next;
  }

  function currentActionCountMap() {
    try {
      const parsed = JSON.parse(localStorage.getItem(actionCountKey) || "{}");
      if (parsed && typeof parsed === "object") {
        return parsed;
      }
    } catch (_) {
      // Ignore corrupt localStorage state.
    }
    return {};
  }

  function nextActionCount(action) {
    const data = currentActionCountMap();
    const next = safeInt(data[action]) + 1;
    data[action] = next;
    localStorage.setItem(actionCountKey, JSON.stringify(data));
    return next;
  }

  function send(payload) {
    const body = JSON.stringify(payload);

    if (navigator.sendBeacon) {
      const blob = new Blob([body], { type: "application/json" });
      const ok = navigator.sendBeacon(endpoint, blob);
      if (ok) {
        return;
      }
    }

    fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body,
      keepalive: true
    }).catch(function () {
      // Swallow analytics failures to avoid breaking UI flows.
    });
  }

  function track(eventType, action, count) {
    // Intentionally minimal payload: no user-agent, no fingerprint, no metadata blob.
    send({
      page,
      eventType,
      action,
      count,
      timestamp: new Date().toISOString()
    });
  }

  const pageLoadCount = nextCount(loadCountKey);
  track("page_load", "initial", pageLoadCount);

  document.addEventListener("click", function (event) {
    const target = event.target.closest("a, button, [data-track-action]");
    if (!target) {
      return;
    }

    const action =
      target.getAttribute("data-track-action") ||
      target.id ||
      target.getAttribute("href") ||
      target.textContent ||
      target.tagName;

    const normalized = String(action).trim().slice(0, 120) || "click";
    const count = nextActionCount("click:" + normalized);
    track("click", normalized, count);
  });

  document.addEventListener("submit", function (event) {
    const form = event.target;
    const action = form.id || form.getAttribute("name") || "form_submit";
    const count = nextActionCount("submit:" + action);
    track("submit", action, count);
  });

  window.addEventListener("hashchange", function () {
    const action = window.location.hash || "#";
    const count = nextActionCount("hash:" + action);
    track("navigation", action, count);
  });
})();
