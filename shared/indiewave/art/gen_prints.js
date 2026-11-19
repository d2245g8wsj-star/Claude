// Renders a plugin's print layers (spec.json "layers") to <art>/textures/*.png.
//   node shared/indiewave/art/gen_prints.js plugins/tide/art
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const art = path.resolve(process.argv[2]);
  const spec = JSON.parse(fs.readFileSync(path.join(art, 'spec.json'), 'utf8'));
  const b = await chromium.launch(), p = await b.newPage();
  await p.goto('file://' + path.join(__dirname, 'prints.html'));
  const out = await p.evaluate(L => build(L), spec.layers);
  fs.mkdirSync(path.join(art, 'textures'), { recursive: true });
  for (const [k, v] of Object.entries(out)) {
    fs.writeFileSync(path.join(art, 'textures', k + '.png'), Buffer.from(v.split(',')[1], 'base64'));
    console.log('textures/' + k + '.png');
  }
  await b.close();
})();
