// Settings page: picks the serial device + baud rate and POSTs them to
// the daemon, which saves to its config file and reopens the port.
(function () {
  const portSel       = document.getElementById("portSel");
  const baudSel       = document.getElementById("baudSel");
  const refreshBtn    = document.getElementById("refreshBtn");
  const applyBtn      = document.getElementById("applyBtn");
  const disconnectBtn = document.getElementById("disconnectBtn");
  const forgetBtn     = document.getElementById("forgetBtn");
  const portHint      = document.getElementById("portHint");
  const led           = document.getElementById("led");
  const statusText    = document.getElementById("statusText");

  let portCache = [];   // most-recent /api/ports response

  function formatPortLabel(p) {
    const star = p.likelySpe ? "★ " : "";
    let desc = p.description || p.manufacturer || "";
    if (p.vid !== undefined && p.pid !== undefined) {
      const hex = (v) => v.toString(16).padStart(4, "0").toUpperCase();
      desc = desc
        ? `${desc} (VID ${hex(p.vid)} PID ${hex(p.pid)})`
        : `VID ${hex(p.vid)} PID ${hex(p.pid)}`;
    }
    return desc ? `${star}${p.name} — ${desc}` : `${star}${p.name}`;
  }

  function populatePorts(ports, selected) {
    portCache = ports;
    portSel.innerHTML = "";
    if (!ports.length) {
      const opt = document.createElement("option");
      opt.textContent = "(no serial ports detected)";
      opt.disabled = true;
      portSel.appendChild(opt);
      portHint.textContent = "Plug in the amplifier's USB-to-serial cable, then ↻.";
      return;
    }
    ports.forEach((p) => {
      const opt = document.createElement("option");
      opt.value = p.name;
      opt.textContent = formatPortLabel(p);
      portSel.appendChild(opt);
    });

    // Prefer the saved selection, else first FTDI (★), else first entry.
    const preferred = ports.find((p) => p.name === selected);
    const firstFtdi = ports.find((p) => p.likelySpe);
    const pick = preferred || firstFtdi || ports[0];
    portSel.value = pick.name;
    portHint.textContent = pick.likelySpe
      ? "★ looks like the SPE amp's FTDI chip."
      : "";
  }

  async function fetchPorts() {
    const res = await fetch("/api/ports");
    return await res.json();
  }

  async function fetchConfig() {
    const res = await fetch("/api/config");
    return await res.json();
  }

  function renderStatus(cfg) {
    led.classList.remove("ok", "warn", "err");
    if (cfg.connected) {
      led.classList.add("ok");
      statusText.textContent = `Connected — ${cfg.port} @ ${cfg.baud}`;
    } else if (cfg.stopped) {
      // User clicked Disconnect. Don't scream "error", just say so.
      led.classList.add("warn");
      statusText.textContent = cfg.port
        ? `Disconnected (${cfg.port})`
        : "Disconnected";
    } else if (cfg.lastError) {
      led.classList.add("err");
      statusText.textContent = cfg.lastError;
    } else if (!cfg.port) {
      led.classList.add("warn");
      statusText.textContent = "No port selected";
    } else {
      led.classList.add("warn");
      statusText.textContent = `Connecting to ${cfg.port}…`;
    }
  }

  async function refresh() {
    try {
      const [ports, cfg] = await Promise.all([fetchPorts(), fetchConfig()]);
      populatePorts(ports, cfg.port);
      if (cfg.baud) { baudSel.value = String(cfg.baud); }
      renderStatus(cfg);
    } catch (err) {
      statusText.textContent = "Cannot reach daemon: " + err;
      led.classList.remove("ok", "warn"); led.classList.add("err");
    }
  }

  async function apply() {
    applyBtn.disabled = true;
    applyBtn.textContent = "Applying…";
    try {
      const body = {
        port: portSel.value,
        baud: Number(baudSel.value),
      };
      const res = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      const data = await res.json();
      if (!data.ok) {
        statusText.textContent = data.error || "Apply failed";
        led.classList.remove("ok", "warn"); led.classList.add("err");
      }
    } catch (err) {
      statusText.textContent = "Error: " + err;
    } finally {
      applyBtn.disabled = false;
      applyBtn.textContent = "Apply";
      // Give the daemon a beat to attempt the open, then pull fresh state.
      setTimeout(refresh, 400);
    }
  }

  async function disconnect() {
    await fetch("/api/disconnect", { method: "POST" });
    setTimeout(refresh, 200);
  }

  // Clears the saved serial-device name from the daemon's config file and
  // stops the amp controller. After this the daemon boots idle until the
  // user picks a new port from the dropdown and clicks Apply — handy if
  // the Pi gets moved to a different host or the FTDI cable changes slot.
  async function forgetSavedPort() {
    if (!confirm(
          "Remove the saved serial device from the daemon's config?\n\n" +
          "The daemon will disconnect and start idle on next boot until a " +
          "new port is selected here.")) {
      return;
    }
    forgetBtn.disabled = true;
    const prevText = forgetBtn.textContent;
    forgetBtn.textContent = "Forgetting…";
    try {
      const res = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ port: "" }),
      });
      const data = await res.json();
      if (!data.ok) {
        statusText.textContent = data.error || "Forget failed";
        led.classList.remove("ok", "warn"); led.classList.add("err");
      }
      // Daemon will re-open with empty port (which is a no-op); tell it
      // to stop cleanly so isStopped() is true and we render "Disconnected".
      await fetch("/api/disconnect", { method: "POST" });
    } catch (err) {
      statusText.textContent = "Error: " + err;
    } finally {
      forgetBtn.disabled = false;
      forgetBtn.textContent = prevText;
      setTimeout(refresh, 300);
    }
  }

  refreshBtn.addEventListener("click", refresh);
  applyBtn.addEventListener("click", apply);
  disconnectBtn.addEventListener("click", disconnect);
  forgetBtn.addEventListener("click", forgetSavedPort);
  portSel.addEventListener("change", () => {
    const p = portCache.find((x) => x.name === portSel.value);
    portHint.textContent = p && p.likelySpe
      ? "★ looks like the SPE amp's FTDI chip."
      : "";
  });

  refresh();
  // Poll status so the user sees open success/failure land without
  // having to click Refresh.
  setInterval(async () => {
    try { renderStatus(await fetchConfig()); } catch (_) {}
  }, 1500);
})();
