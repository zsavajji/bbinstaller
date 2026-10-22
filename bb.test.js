// A wrong hash costs a password attempt on the phone, so pin it to Sachesi's algorithm.
// Vector from a line-for-line C mirror of InstallNet::HashPass (src/installer.cpp:1531).
const assert = require('assert');
const { hashPass } = require('./bb');

assert.strictEqual(
  hashPass('hunter2', 'ABCDEF0123456789', Buffer.from('deadbeef01020304', 'hex'), 1000).toString('hex').toUpperCase(),
  'CDB08C3197BA5BCA3BA0EF335194995A78FF564B7CF7FCC3C697BC483405B457B910DCAFEBABB593596DA67F2BBE5B8E02B332760AE84D67D5A3C2FF05E1EEC3');
console.log('hashPass ok');
