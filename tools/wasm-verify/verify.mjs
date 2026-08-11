// Renders a chart through the WebAssembly module and reports what came out.
//
// This is the headless counterpart to the browser build: it loads the same
// renderojn.js the web app loads, so anything it proves holds for the site.
// Pair it with the CLI to compare outputs (see README.md).
//
// Usage:
//   node verify.mjs <chart.ojn> <package.ojm> [difficulty] [wav|mp3|ogg] [outdir]
//
// difficulty: 0 easy, 1 normal, 2 hard (default 2)

import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const MODULE_URL = process.env.RENDEROJN_WASM
  ? pathToFileURL(process.env.RENDEROJN_WASM).href
  : pathToFileURL(path.resolve('out/build/wasm/renderojn.js')).href;

const [, , ojnPath, ojmPath, difficultyArg = '2', formatArg = 'wav', outDir = '.'] = process.argv;

if (!ojnPath || !ojmPath) {
  console.error('usage: node verify.mjs <chart.ojn> <package.ojm> [0|1|2] [wav|mp3|ogg] [outdir]');
  console.error('       set RENDEROJN_WASM to point at a renderojn.js outside ./out/build/wasm');
  process.exit(2);
}

const FORMATS = { wav: 0, mp3: 1, ogg: 2 };
if (!(formatArg in FORMATS)) {
  console.error(`unknown format "${formatArg}" (expected wav, mp3, or ogg)`);
  process.exit(2);
}

const { default: createRenderOJN } = await import(MODULE_URL);
const module = await createRenderOJN();

// Embind surfaces a thrown renderojn::Error as a pointer-shaped object; turn it
// back into the message the core actually produced.
const explain = (error) => {
  if (error && typeof error === 'object' && 'excPtr' in error) {
    try {
      return module.getExceptionMessage(error.excPtr).filter(Boolean).join(': ');
    } catch {
      return `C++ exception at ${error.excPtr}`;
    }
  }
  return String(error?.message ?? error);
};

const toVector = (vector) => {
  const items = [];
  for (let index = 0; index < vector.size(); ++index) items.push(vector.get(index));
  return items;
};

const ojn = new Uint8Array(readFileSync(ojnPath));
const ojm = new Uint8Array(readFileSync(ojmPath));

let info;
try {
  // Header-only read: what the UI calls the moment a chart is dropped, before
  // its sample package has necessarily arrived.
  info = module.readOjnInfo(ojn);
} catch (error) {
  console.error(`readOjnInfo failed: ${explain(error)}`);
  process.exit(1);
}

console.log(`title:    ${info.title}`);
console.log(`artist:   ${info.artist}`);
console.log(`charter:  ${info.charter}`);
console.log(`package:  ${info.packageName}`);
console.log(`genre:    ${info.genre}   tempo: ${info.tempo.toFixed(2)}   song id: ${info.songId}`);
console.log(
  `levels:   ${toVector(info.difficulties)
    .map((d) => `${d.difficulty}(${d.noteCount} notes, ${d.durationSeconds}s)`)
    .join('  ')}`,
);
for (const warning of toVector(info.warnings)) console.log(`warning:  ${warning}`);

let reported = -1;
const started = Date.now();
let result;
try {
  result = module.render(ojn, ojm, Number(difficultyArg), FORMATS[formatArg], 3, (fraction) => {
    const percent = Math.floor(fraction * 10) * 10;
    if (percent > reported) {
      reported = percent;
      process.stdout.write(`\rrender:   ${percent}%`);
    }
  });
} catch (error) {
  console.error(`\nrender failed: ${explain(error)}`);
  process.exit(1);
}

const bytes = Buffer.from(result.bytes);
const elapsed = ((Date.now() - started) / 1000).toFixed(1);
process.stdout.write('\r');
console.log(`render:   ${bytes.length} bytes in ${elapsed}s`);
for (const warning of toVector(result.warnings)) console.log(`warning:  ${warning}`);
console.log(`sha256:   ${createHash('sha256').update(bytes).digest('hex')}`);

const stem = path.basename(ojnPath).replace(/\.ojn$/i, '');
const destination = path.join(outDir, `${stem}-wasm.${formatArg}`);
writeFileSync(destination, bytes);
console.log(`wrote:    ${destination}`);
