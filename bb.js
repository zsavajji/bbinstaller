// BB10 desktop protocol, ported from Sachesi's InstallNet (src/installer.cpp).
// The OS brings up the phone's USB network link (macOS: CDC-NCM, Linux: rndis_host/cdc_ncm);
// this only speaks the phone's cgi-bin protocol: discovery -> login -> update.cgi.
const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const https = require('https');
const os = require('os');
const path = require('path');
const zlib = require('zlib');

const UA = 'QNXWebClient/1.0';
// The phone only speaks TLS 1.0 (AES256-SHA) and has a self-signed cert
const agent = new https.Agent({ minVersion: 'TLSv1', rejectUnauthorized: false });

const tag = (xml, name) => xml.match(new RegExp(`<${name}>([^<]*)</${name}>`))?.[1];
const has = (xml, name) => xml.includes(`<${name}>`);

// InstallNet::HashPass: SHA-512 chain over (count, salt, data), twice; the second pass is seeded with the challenge
function hashPass(password, challenge, salt, iterations) {
  let data = Buffer.from(password, 'latin1');
  for (const prefix of [Buffer.from(challenge, 'latin1'), Buffer.alloc(0)]) {
    for (let i = 0; i < iterations; i++) {
      const count = Buffer.alloc(4);
      count.writeInt32LE(i);
      data = crypto.createHash('sha512').update(Buffer.concat([count, salt, data])).digest();
    }
    data = Buffer.concat([prefix, data]);
  }
  return data;
}

// Just enough zip to pull META-INF/MANIFEST.MF out of a .bar without reading the whole file
function readManifest(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const read = (pos, len) => { const b = Buffer.alloc(len); fs.readSync(fd, b, 0, len, pos); return b; };
    const size = fs.fstatSync(fd).size;
    const tail = read(Math.max(0, size - 65557), Math.min(size, 65557));
    const eocd = tail.lastIndexOf(Buffer.from('PK\x05\x06', 'latin1'));
    if (eocd < 0) throw new Error('not a zip file');
    const cd = read(tail.readUInt32LE(eocd + 16), tail.readUInt32LE(eocd + 12));
    for (let p = 0; p + 46 <= cd.length; p += 46 + cd.readUInt16LE(p + 28) + cd.readUInt16LE(p + 30) + cd.readUInt16LE(p + 32)) {
      if (cd.toString('latin1', p + 46, p + 46 + cd.readUInt16LE(p + 28)) !== 'META-INF/MANIFEST.MF') continue;
      const local = cd.readUInt32LE(p + 42);
      const lh = read(local, 30);
      const data = read(local + 30 + lh.readUInt16LE(26) + lh.readUInt16LE(28), cd.readUInt32LE(p + 20));
      return (cd.readUInt16LE(p + 10) === 8 ? zlib.inflateRawSync(data) : data).toString('utf8');
    }
    throw new Error('no META-INF/MANIFEST.MF');
  } finally {
    fs.closeSync(fd);
  }
}

// Refuses OS/radio bars: Sachesi sends those with mode=os, this app only does apps
function barInfo(file) {
  const info = { path: file, name: path.basename(file), size: 0 };
  try {
    info.size = fs.statSync(file).size;
    const fields = Object.fromEntries(readManifest(file).split(/\r?\n/)
      .filter(l => l.includes(':')).map(l => [l.slice(0, l.indexOf(':')).trim(), l.slice(l.indexOf(':') + 1).trim()]));
    info.name = fields['Package-Name'] || info.name;
    info.version = fields['Package-Version'];
    if ([fields['Package-Type'], fields['Patch-Package-Type']].includes('system')) info.error = 'OS/radio bar: use Sachesi';
  } catch (e) {
    info.error = `not an installable .bar (${e.message})`;
  }
  return info;
}

function request(url, { body, type = 'x-www-form-urlencoded', cookies = {}, timeout = 600000, onSent } = {}) {
  return new Promise((resolve, reject) => {
    const headers = { 'User-Agent': UA, 'Content-Type': 'application/' + type };
    if (Object.keys(cookies).length) headers.Cookie = Object.entries(cookies).map(([k, v]) => `${k}=${v}`).join('; ');
    const file = body?.file;
    if (file) headers['Content-Length'] = fs.statSync(file).size;
    else if (body !== undefined) headers['Content-Length'] = Buffer.byteLength(body);
    const lib = url.startsWith('https:') ? https : http;
    const req = lib.request(url, { method: body === undefined ? 'GET' : 'POST', headers, agent: lib === https ? agent : undefined, timeout }, res => {
      for (const c of res.headers['set-cookie'] ?? []) {
        const kv = c.split(';')[0];
        cookies[kv.slice(0, kv.indexOf('=')).trim()] = kv.slice(kv.indexOf('=') + 1);
      }
      let xml = '';
      res.setEncoding('utf8');
      res.on('data', d => xml += d);
      res.on('end', () => resolve(xml));
    });
    req.on('timeout', () => req.destroy(new Error('phone did not answer')));
    req.on('error', reject);
    if (file) {
      let sent = 0;
      fs.createReadStream(file).on('data', d => onSent?.(sent += d.length)).on('error', reject).pipe(req);
    } else {
      req.end(body);
    }
  });
}

// Sachesi's guess: the phone sits one address below ours on the USB link
function candidateIps() {
  return Object.values(os.networkInterfaces()).flat()
    .filter(a => a.family === 'IPv4' && a.address.startsWith('169.254.'))
    .map(a => { const p = a.address.split('.').map(Number); p[3] = p[3] - 1 || 128; return p.join('.'); });
}

// Call once per connection. HTTP discovery has the details, but the phone stops serving
// port 80 after a while (seen after ~60 polls), so fall back to "login.cgi answers over HTTPS".
async function identify(ip) {
  try {
    const xml = await request(`http://${ip}/cgi-bin/discovery.cgi`, { timeout: 2000 });
    if (has(xml, 'DeviceCharacteristics')) return {
      ip,
      model: tag(xml, 'ModelName'),
      os: tag(xml, 'PlatformVersion'),
      protocol: Number(tag(xml, 'MaxSupportedProtocolVersion')) >= 3 ? 3 : 1,
      setupDone: tag(xml, 'OobeCompleted') !== '0',
    };
  } catch {}
  try {
    const xml = await request(`https://${ip}/cgi-bin/login.cgi?request_version=3`, { timeout: 3000 });
    if (has(xml, 'RimTabletResponse')) return { ip, model: 'BlackBerry 10', os: '', protocol: 3, setupDone: true };
  } catch {}
  return null;
}

class Device {
  constructor(info) {
    this.info = info;
    this.cookies = {};
  }

  async call(page, opts = {}) {
    const xml = await request(`https://${this.info.ip}/cgi-bin/${page}`, { ...opts, cookies: this.cookies });
    if (tag(xml, 'Status') === 'Error' || has(xml, 'ErrorDescription')) throw new Error(tag(xml, 'ErrorDescription') || xml);
    return xml;
  }

  post(page, form = {}) {
    return this.call(page, { body: new URLSearchParams(form).toString() });
  }

  async login(password) {
    const v = this.info.protocol;
    let xml = await this.call(`login.cgi?request_version=${v}`);
    if (has(xml, 'AuthChallenge')) {
      const digest = hashPass(password, tag(xml, 'Challenge'), Buffer.from(tag(xml, 'Salt'), 'hex'), Number(tag(xml, 'ICount')));
      xml = await this.call(`login.cgi?challenge_data=${digest.toString('hex').toUpperCase()}&request_version=${v}`);
    }
    if (tag(xml, 'Status') !== 'Success') {
      const e = new Error(`Wrong password. Not retrying: too many wrong attempts wipe the phone. (${xml.replace(/\s+/g, ' ')})`);
      e.wrongPassword = true;
      throw e;
    }
  }

  // progress({ index, phase: 'upload' | 'install' | 'done', percent })
  async install(files, progress) {
    let xml = await this.post('update.cgi', { mode: 'bar', size: files.reduce((n, f) => n + fs.statSync(f).size, 0) });
    if (!has(xml, 'UpdateStart')) throw new Error('Phone refused the update: ' + xml);
    for (const [index, file] of files.entries()) {
      const size = fs.statSync(file).size;
      xml = await this.call('update.cgi?type=bar', {
        body: { file }, type: 'octet-stream',
        onSent: sent => progress({ index, phase: 'upload', percent: Math.round(100 * sent / size) }),
      });
      // Phone answers UpdateSend, then UpdateProgress (InProgress...) until Success
      while (!(has(xml, 'UpdateProgress') && tag(xml, 'Status') === 'Success')) {
        progress({ index, phase: 'install', percent: Number(tag(xml, 'Progress') ?? 0) });
        await new Promise(r => setTimeout(r, 500));
        xml = await this.post('update.cgi');
      }
      progress({ index, phase: 'done', percent: 100 });
    }
    await this.post('update.cgi', { status: 'success' });
  }
}

module.exports = { hashPass, barInfo, candidateIps, identify, Device };
