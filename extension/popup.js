const DEFAULTS = { enabled: true, minSizeKB: 0, skipExts: 'crx', skipHosts: '', stats: 0, lastError: '' };
const $ = (id) => document.getElementById(id);
const t = (key, ...subs) => chrome.i18n.getMessage(key, subs);

// Localize static text and set the page direction from the UI language.
const uiLang = chrome.i18n.getUILanguage();
document.documentElement.lang = uiLang;
document.documentElement.dir = uiLang.startsWith('ar') ? 'rtl' : 'ltr';
document.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });

function renderState(on) {
  $('stateText').textContent = on ? t('captureOn') : t('captureOff');
}

chrome.storage.local.get(DEFAULTS).then((cfg) => {
  $('enabled').checked = cfg.enabled;
  $('minSizeKB').value = cfg.minSizeKB || '';
  $('skipExts').value = cfg.skipExts;
  $('skipHosts').value = cfg.skipHosts;
  $('stats').textContent = cfg.stats ? t('stats', String(cfg.stats)) : '';
  renderState(cfg.enabled);
});

$('enabled').addEventListener('change', (e) => {
  chrome.storage.local.set({ enabled: e.target.checked });
  renderState(e.target.checked);
});
$('minSizeKB').addEventListener('change', (e) => chrome.storage.local.set({ minSizeKB: Math.max(0, +e.target.value || 0) }));
$('skipExts').addEventListener('change', (e) => chrome.storage.local.set({ skipExts: e.target.value }));
$('skipHosts').addEventListener('change', (e) => chrome.storage.local.set({ skipHosts: e.target.value }));

chrome.runtime.sendMessage({ type: 'ping' }, (resp) => {
  const box = $('status');
  box.classList.remove('checking');
  if (resp && resp.ok) {
    box.classList.add('ok');
    $('statusText').textContent = resp.running ? t('statusRunning') : t('statusReady');
  } else {
    box.classList.add('bad');
    $('statusText').textContent = t('statusMissing');
  }
});
