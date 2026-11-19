// Renders the print layers in prints.html to textures/*.png.
// Run from anywhere: node plugins/nazare/art/gen_prints.js
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const dir = __dirname, L = JSON.parse(fs.readFileSync(path.join(dir, 'layout.json'), 'utf8'));
  const b = await chromium.launch(), p = await b.newPage();
  await p.goto('file://' + path.join(dir, 'prints.html'));
  const out = await p.evaluate(L => build(L), L);
  for (const [k, v] of Object.entries(out)) {
    fs.writeFileSync(path.join(dir, 'textures', k + '.png'), Buffer.from(v.split(',')[1], 'base64'));
    console.log('textures/' + k + '.png');
  }
  await b.close();
})();
