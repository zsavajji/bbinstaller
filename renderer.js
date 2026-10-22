const $ = id => document.getElementById(id);
let device = null;
let bars = []; // barInfo + { el, state }
let busy = false;

const mb = n => (n / 1e6).toFixed(1) + ' MB';
const valid = () => bars.filter(b => !b.error && b.state !== 'done');

function setStatus(text, error = false) {
  $('status').textContent = text;
  $('status').className = error ? 'err' : '';
}

function refresh() {
  $('device').className = device ? 'on' : '';
  $('model').textContent = device ? device.model : 'No phone connected';
  $('detail').textContent = device
    ? [device.os && `OS ${device.os}`, device.ip, !device.setupDone && 'finish setup on the phone first'].filter(Boolean).join(' · ')
    : 'Plug in your BlackBerry 10 with a USB cable';
  $('install').disabled = busy || !device || !valid().length;
  $('drop').classList.toggle('busy', busy);
  $('password').disabled = busy;
}

function add(infos) {
  for (const info of infos) {
    if (bars.some(b => b.path === info.path)) continue;
    const el = document.createElement('div');
    el.className = 'bar' + (info.error ? ' bad' : '');
    el.innerHTML = '<span class="name"></span><span class="ver"></span><button title="Remove">×</button><small></small>';
    el.querySelector('.name').textContent = info.name;
    el.querySelector('.ver').textContent = info.version ?? '';
    el.querySelector('small').textContent = info.error ?? mb(info.size);
    const bar = { ...info, el };
    el.querySelector('button').onclick = e => {
      e.stopPropagation();
      if (busy) return;
      bars = bars.filter(b => b !== bar);
      el.remove();
      refresh();
    };
    el.onclick = e => e.stopPropagation();
    bars.push(bar);
    $('drop').append(el);
  }
  refresh();
}

$('drop').onclick = async () => !busy && add(await bb.pick());
$('drop').ondragover = e => { e.preventDefault(); $('drop').classList.add('over'); };
$('drop').ondragleave = () => $('drop').classList.remove('over');
$('drop').ondrop = async e => {
  e.preventDefault();
  $('drop').classList.remove('over');
  if (!busy) add(await Promise.all([...e.dataTransfer.files].map(f => bb.info(f))));
};
$('password').oninput = refresh;

let queue = [];
bb.on('progress', ({ index, phase, percent }) => {
  const bar = queue[index];
  let p = bar.el.querySelector('progress');
  if (!p) bar.el.append(p = Object.assign(document.createElement('progress'), { max: 100 }));
  p.value = percent;
  bar.el.querySelector('small').textContent =
    phase === 'upload' ? `Sending… ${percent}%` : phase === 'install' ? `Installing… ${percent}%` : 'Installed';
  if (phase === 'done') { bar.state = 'done'; bar.el.classList.add('done'); p.remove(); }
});

$('install').onclick = async () => {
  queue = valid();
  busy = true;
  refresh();
  setStatus(`Installing ${queue.length} app${queue.length > 1 ? 's' : ''}…`);
  try {
    await bb.install(queue.map(b => b.path), $('password').value);
    setStatus('Done.');
  } catch (e) {
    const msg = e.message.replace(/^Error invoking remote method 'install': (Error: )?/, '');
    setStatus(msg, true);
    // A wrong password costs an attempt: make the user retype before trying again
    if (/Wrong password/.test(msg)) $('password').value = '';
    for (const b of queue) if (b.state !== 'done') b.el.querySelector('progress')?.remove();
  }
  busy = false;
  refresh();
};

bb.on('device', d => { device = d; refresh(); });
bb.device().then(d => { device = d; refresh(); });
