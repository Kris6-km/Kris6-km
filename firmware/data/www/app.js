const $ = (s) => document.querySelector(s);
const parityStr = (p) => ['N','E','O'][p|0] || 'N';

// Tabs
document.querySelectorAll('.tab').forEach(t => {
  t.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(x => x.classList.remove('active'));
    t.classList.add('active');
    document.getElementById(t.dataset.tab).classList.add('active');
  });
});

async function refreshStatus() {
  try {
    const r = await fetch('/api/status');
    const j = await r.json();
    $('#ip').textContent = j.ip + (j.sd ? '  •  SD OK' : '  •  SD —');
    $('#status').textContent = j.busy ? 'Skanowanie...' : 'Gotowy';
    const tbody = $('#devices tbody');
    tbody.innerHTML = '';
    (j.devices || []).forEach(d => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${d.id}</td><td>${d.baud}</td><td>${parityStr(d.parity)}</td><td>${d.stop}</td>
                      <td><button data-id="${d.id}" class="ghost">Rejestry →</button></td>`;
      tr.querySelector('button').addEventListener('click', () => {
        $('#r_id').value = d.id;
        document.querySelector('[data-tab=regs]').click();
      });
      tbody.appendChild(tr);
    });
  } catch (e) {}
}

async function loadSettings() {
  const r = await fetch('/api/settings');
  const s = await r.json();
  $('#s_baud').value = s.baudrate;
  $('#s_parity').value = s.parity;
  $('#s_stop').value = s.stopBits;
  $('#s_first').value = s.scanFirst;
  $('#s_last').value = s.scanLast;
  $('#s_autobaud').checked = !!s.autoBaud;
  $('#s_logsd').checked = !!s.logToSd;
  $('#s_ssid').value = s.wifiSsid || '';
}

$('#btnScan').addEventListener('click', async () => {
  $('#status').textContent = 'Startuje skan...';
  await fetch('/api/scan', { method: 'POST' });
  setTimeout(refreshStatus, 500);
});
$('#btnRefresh').addEventListener('click', refreshStatus);

$('#btnSave').addEventListener('click', async () => {
  const payload = {
    baudrate: Number($('#s_baud').value),
    parity:   Number($('#s_parity').value),
    stopBits: Number($('#s_stop').value),
    scanFirst: Number($('#s_first').value),
    scanLast:  Number($('#s_last').value),
    autoBaud: $('#s_autobaud').checked,
    logToSd:  $('#s_logsd').checked,
    wifiSsid: $('#s_ssid').value,
    wifiPass: $('#s_pass').value,
  };
  await fetch('/api/settings', {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify(payload)
  });
  alert('Zapisano. Zmiany WiFi wymagają restartu.');
});

$('#btnRead').addEventListener('click', async () => {
  const q = new URLSearchParams({
    id: $('#r_id').value, type: $('#r_type').value,
    start: $('#r_start').value, count: $('#r_count').value
  });
  const r = await fetch('/api/read?' + q);
  const j = await r.json();
  const tbody = $('#regTable tbody');
  tbody.innerHTML = '';
  (j.values || []).forEach(v => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${v.addr}</td><td>${v.value}</td><td>0x${v.value.toString(16).padStart(4,'0').toUpperCase()}</td>
                    <td style="color:${v.ok?'var(--ok)':'var(--danger)'}">${v.ok ? 'OK' : (j.error||'err')}</td>`;
    tbody.appendChild(tr);
  });
});

loadSettings();
refreshStatus();

// ====================== CAN ======================
let canPolling = null;
const hexId = (n) => n.extended
  ? '0x' + n.id.toString(16).toUpperCase().padStart(8, '0')
  : '0x' + n.id.toString(16).toUpperCase().padStart(3, '0');

async function refreshCan() {
  try {
    const j = await (await fetch('/api/can/status')).json();
    $('#canStatus').textContent = j.busy ? 'Nasłuch...' : `Gotowy (${j.bitrate} bps)`;
    const tbody = $('#canTable tbody');
    tbody.innerHTML = '';
    (j.nodes || []).forEach(n => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${hexId(n)}</td><td>${n.extended ? '29-bit' : '11-bit'}</td>
                      <td>${n.dlc}</td><td>${n.frames}</td><td>${n.data || ''}</td>`;
      tbody.appendChild(tr);
    });
    if (!j.busy && canPolling) { clearInterval(canPolling); canPolling = null; }
    return j;
  } catch (e) {}
}

(async () => {
  const j = await refreshCan();
  if (j) {
    $('#c_bitrate').value = j.bitrate;
    $('#c_ms').value = j.scanMs;
    $('#c_auto').checked = !!j.autobaud;
  }
})();

$('#btnCanScan').addEventListener('click', async () => {
  const r = await fetch('/api/can/scan', {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify({
      bitrate: Number($('#c_bitrate').value),
      scanMs: Number($('#c_ms').value),
      autobaud: $('#c_auto').checked,
    })
  });
  if (r.status === 409) { $('#canStatus').textContent = 'Zajęty'; return; }
  $('#canStatus').textContent = 'Nasłuch...';
  if (!canPolling) canPolling = setInterval(refreshCan, 1000);
});

$('#btnCanSend').addEventListener('click', async () => {
  const r = await fetch('/api/can/send', {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify({
      id: parseInt($('#c_tx_id').value, 16),
      extended: $('#c_tx_ext').checked,
      data: $('#c_tx_data').value,
      bitrate: Number($('#c_bitrate').value),
    })
  });
  const j = await r.json();
  $('#canTxOut').textContent = j.ok ? 'Wysłano.' : ('Błąd: ' + (j.error || r.status));
});

// ====================== AI Assistant ======================
const AI_URL_KEY = 'ai_backend_url';
const AI_AUTO_KEY = 'ai_autoanalyze';
const AI_DEFAULT_URL = 'https://rs485-modbus-scan.preview.emergentagent.com';
let lastDevices = [];
let lastRegs = [];
let lastCmd = null;
let busyStatus = false;

$('#ai_url').value = localStorage.getItem(AI_URL_KEY) || AI_DEFAULT_URL;
$('#ai_autoanalyze').checked = localStorage.getItem(AI_AUTO_KEY) === '1';
$('#ai_url').addEventListener('change', () => localStorage.setItem(AI_URL_KEY, $('#ai_url').value.trim()));
$('#ai_autoanalyze').addEventListener('change', () => localStorage.setItem(AI_AUTO_KEY, $('#ai_autoanalyze').checked ? '1' : '0'));

function aiUrl() { return ($('#ai_url').value.trim() || AI_DEFAULT_URL).replace(/\/$/, ''); }

async function streamAI(path, payload, outEl) {
  outEl.textContent = '';
  const resp = await fetch(aiUrl() + path, {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify(payload),
  });
  if (!resp.ok || !resp.body) { outEl.textContent = 'Błąd AI: HTTP ' + resp.status; return; }
  const reader = resp.body.getReader();
  const dec = new TextDecoder();
  let buf = '';
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    buf += dec.decode(value, { stream: true });
    const lines = buf.split('\n\n');
    buf = lines.pop();
    for (const chunk of lines) {
      const line = chunk.trim();
      if (!line.startsWith('data:')) continue;
      try {
        const j = JSON.parse(line.slice(5).trim());
        if (j.delta) { outEl.textContent += j.delta; outEl.scrollTop = outEl.scrollHeight; }
        if (j.error) { outEl.textContent += '\n[Błąd: ' + j.error + ']'; }
      } catch(e) {}
    }
  }
}

async function aiAnalyzeLast() {
  if (!lastDevices.length) { $('#aiStream').textContent = 'Brak wyników skanu. Uruchom skanowanie najpierw.'; return; }
  await streamAI('/api/ai/analyze-scan', {
    baud: lastDevices[0]?.baud || null,
    devices: lastDevices,
  }, $('#aiStream'));
}

$('#btnAiAnalyze').addEventListener('click', aiAnalyzeLast);

$('#btnAiChat').addEventListener('click', async () => {
  const prompt = $('#ai_prompt').value.trim();
  if (!prompt) return;
  await streamAI('/api/ai/chat', {
    prompt, devices: lastDevices, registers: lastRegs,
  }, $('#aiStream'));
});

$('#btnAiNl').addEventListener('click', async () => {
  const text = $('#ai_nlcmd').value.trim();
  if (!text) return;
  $('#aiCmdOut').textContent = 'Myślę...';
  try {
    const r = await fetch(aiUrl() + '/api/ai/nl-command', {
      method: 'POST', headers: {'Content-Type':'application/json'},
      body: JSON.stringify({ text, known_devices: lastDevices })
    });
    const j = await r.json();
    if (j.detail) { $('#aiCmdOut').textContent = 'Błąd: ' + j.detail; return; }
    lastCmd = j;
    $('#aiCmdOut').textContent = JSON.stringify(j, null, 2);
  } catch (e) { $('#aiCmdOut').textContent = 'Błąd: ' + e.message; }
});

$('#btnAiRun').addEventListener('click', async () => {
  if (!lastCmd) { alert('Najpierw wygeneruj zapytanie przyciskiem "Zbuduj zapytanie"'); return; }
  // Fill the Rejestry tab and run
  $('#r_id').value = lastCmd.slaveId;
  $('#r_type').value = lastCmd.regType;
  $('#r_start').value = lastCmd.startAddr;
  $('#r_count').value = lastCmd.count;
  document.querySelector('[data-tab=regs]').click();
  $('#btnRead').click();
});

// Hook: remember last devices & registers; trigger autoanalyze when scan finishes
const _origRefresh = refreshStatus;
window.refreshStatus = async function() {
  try {
    const r = await fetch('/api/status');
    const j = await r.json();
    $('#ip').textContent = j.ip + (j.sd ? '  •  SD OK' : '  •  SD —');
    $('#status').textContent = j.busy ? 'Skanowanie...' : 'Gotowy';
    const tbody = $('#devices tbody');
    tbody.innerHTML = '';
    (j.devices || []).forEach(d => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${d.id}</td><td>${d.baud}</td><td>${parityStr(d.parity)}</td><td>${d.stop}</td>
                      <td><button data-id="${d.id}" class="ghost">Rejestry →</button></td>`;
      tr.querySelector('button').addEventListener('click', () => {
        $('#r_id').value = d.id;
        document.querySelector('[data-tab=regs]').click();
      });
      tbody.appendChild(tr);
    });
    lastDevices = j.devices || [];
    // Auto-analyze transition: busy -> not busy
    if (busyStatus && !j.busy && $('#ai_autoanalyze').checked && lastDevices.length) {
      aiAnalyzeLast();
      document.querySelector('[data-tab=ai]').click();
    }
    busyStatus = !!j.busy;
  } catch(e) {}
};
// Replace timer with new wrapper
clearInterval(window._st); window._st = setInterval(refreshStatus, 2500);

// Capture register reads
const _origRead = $('#btnRead').onclick;
$('#btnRead').addEventListener('click', async () => {
  // The main handler already fired; snapshot current table
  setTimeout(() => {
    const rows = document.querySelectorAll('#regTable tbody tr');
    lastRegs = Array.from(rows).map(tr => {
      const c = tr.querySelectorAll('td');
      return { addr: Number(c[0].textContent), value: Number(c[1].textContent), ok: c[3].textContent.trim() === 'OK' };
    });
  }, 400);
});
