// DAO Download Manager - Chrome extension (Manifest V3)
// Captures browser downloads and hands them to DAO through Native Messaging.

const HOST = 'com.flowpoint.dao';
const DEFAULTS = {
  enabled: true,
  minSizeKB: 0,               // ignore smaller files (0 = capture everything)
  skipExts: 'crx',            // extensions left to Chrome
  skipHosts: '',              // hosts left to Chrome
  stats: 0,                   // number of captured downloads
};

let cfg = { ...DEFAULTS };
const ready = chrome.storage.local.get(DEFAULTS).then((v) => { cfg = v; updateBadge(); });

chrome.storage.onChanged.addListener((changes) => {
  for (const k in changes) cfg[k] = changes[k].newValue;
  updateBadge();
});

function updateBadge() {
  const on = cfg.enabled;
  chrome.action.setIcon({
    path: on
      ? { 16: 'icons/16.png', 32: 'icons/32.png', 48: 'icons/48.png' }
      : { 16: 'icons/16-off.png', 32: 'icons/32-off.png', 48: 'icons/48-off.png' },
  });
  chrome.action.setBadgeText({ text: on ? '' : 'OFF' });
  chrome.action.setBadgeBackgroundColor({ color: '#6b7280' });
}

function sendToDao(msg) {
  return new Promise((resolve) => {
    try {
      chrome.runtime.sendNativeMessage(HOST, msg, (resp) => {
        if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
        else resolve(resp || { ok: false, error: 'no response' });
      });
    } catch (e) {
      resolve({ ok: false, error: String(e) });
    }
  });
}

async function cookieHeader(url) {
  try {
    const list = await chrome.cookies.getAll({ url });
    return list.map((c) => `${c.name}=${c.value}`).join('; ');
  } catch {
    return '';
  }
}

const baseName = (p) => (p || '').split(/[\\/]/).pop();
const splitList = (s) => (s || '').toLowerCase().split(/[\s,;]+/).filter(Boolean);

function shouldSkip(item, url) {
  if (!cfg.enabled) return true;
  if (!/^https?:\/\//i.test(url)) return true;              // leave blob: / data: to Chrome
  if (item.state && item.state !== 'in_progress') return true;
  if (cfg.minSizeKB > 0 && item.totalBytes > 0 && item.totalBytes < cfg.minSizeKB * 1024) return true;

  let host = '', path = '';
  try { const u = new URL(url); host = u.hostname.toLowerCase(); path = u.pathname; } catch {}
  const name = baseName(item.filename) || decodeURIComponent(path.split('/').pop() || '');
  const ext = name.includes('.') ? name.split('.').pop().toLowerCase() : '';
  if (ext && splitList(cfg.skipExts).includes(ext)) return true;
  if (splitList(cfg.skipHosts).some((h) => host === h || host.endsWith('.' + h))) return true;
  return false;
}

// Capture once Chrome has resolved the real file name (Content-Disposition / MIME type).
const handled = new Set();

chrome.downloads.onDeterminingFilename.addListener((item, suggest) => {
  suggest();                       // keep Chrome's suggested name
  handleDownload(item);
});

async function handleDownload(item) {
  await ready;
  if (handled.has(item.id)) return;
  handled.add(item.id);
  const url = item.finalUrl || item.url;
  if (shouldSkip(item, url)) return;

  // pause Chrome's transfer until DAO confirms it took over
  try { await chrome.downloads.pause(item.id); } catch {}

  const resp = await sendToDao({
    type: 'download',
    url: item.url || url,          // original URL (before redirects)
    filename: baseName(item.filename),
    referrer: item.referrer || '',
    cookies: await cookieHeader(item.url || url),
    userAgent: navigator.userAgent,
    size: item.totalBytes > 0 ? item.totalBytes : (item.fileSize > 0 ? item.fileSize : -1),
    mime: item.mime || '',
  });

  if (resp && resp.ok) {
    try { await chrome.downloads.cancel(item.id); } catch {}
    try { await chrome.downloads.erase({ id: item.id }); } catch {}
    chrome.storage.local.set({ stats: (cfg.stats || 0) + 1 });
  } else {
    // DAO unavailable: let Chrome finish the download
    console.warn('[DAO] fallback to Chrome:', resp && resp.error);
    try { await chrome.downloads.resume(item.id); } catch {}
    chrome.storage.local.set({ lastError: (resp && resp.error) || 'unknown' });
  }
}

// Context menu
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: 'dao-download',
    title: chrome.i18n.getMessage('menuDownload'),
    contexts: ['link', 'video', 'audio', 'image'],
  });
  updateBadge();
  sendToDao({ type: 'ping' });       // tell DAO the extension is installed
});

chrome.runtime.onStartup.addListener(() => {
  updateBadge();
  sendToDao({ type: 'ping' });
});

chrome.contextMenus.onClicked.addListener(async (info) => {
  const url = info.linkUrl || info.srcUrl;
  if (!url || !/^https?:\/\//i.test(url)) return;
  const resp = await sendToDao({
    type: 'download',
    url,
    filename: '',
    referrer: info.pageUrl || '',
    cookies: await cookieHeader(url),
    userAgent: navigator.userAgent,
    size: -1,
  });
  if (!resp.ok) chrome.storage.local.set({ lastError: resp.error || 'unknown' });
});

// Messages from the popup
chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg && msg.type === 'ping') {
    sendToDao({ type: 'ping' }).then(sendResponse);
    return true;
  }
  return false;
});
