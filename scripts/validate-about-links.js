// Check the links actually shipped in About and its embedded Markdown documents.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const marked = require(path.join(root, 'assets/menu/vendor/marked.umd.js'));
const html = fs.readFileSync(path.join(root, 'nitlink-menu.html'), 'utf8');
const documents = JSON.parse(html.match(/<script type="application\/json" id="about-document-data">([\s\S]*?)<\/script>/)[1]);
const sourceFiles = { projectLicense: 'LICENSE', licenseNotices: 'LICENSES.md', acknowledgments: 'ACKNOWLEDGMENTS.md' };
for (const [id, file] of Object.entries(sourceFiles)) {
  assert.equal(documents[id].replaceAll('\r\n', '\n'), fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n'), `${file}: embedded document is outdated`);
}
const policy = fs.readFileSync(path.join(root, 'src/app/webview_policy.h'), 'utf8');
const allowlist = policy.match(/ExternalLinks\[\]\s*=\s*\{([\s\S]*?)\};/)[1];
const allowed = new Set([...allowlist.matchAll(/L"([^"]+)"/g)].map(match => match[1]));
const updater = fs.readFileSync(path.join(root, 'src/app/update_checker.h'), 'utf8');
const sources = [...updater.matchAll(/\{L"(projectLicense|licenseNotices|acknowledgments)", L"([^"]+)"\}/g)];
assert.equal(sources.length, 3, 'All three GitHub document sources must be fixed native URLs');
for (const [, id, url] of sources) {
  assert.equal(url, `https://raw.githubusercontent.com/HolyBearTW/nitlink-holybear-edition/refs/heads/holybear/integrated/${sourceFiles[id]}`);
  allowed.add(url);
}
const pages = [html, ...Object.values(documents).map(text => marked.parse(text))];
const links = new Set(pages.flatMap(page => [...page.matchAll(/<a\b[^>]*\bhref="([^"]+)"/g)]
  .map(match => match[1].replaceAll('&amp;', '&'))).filter(url => url.startsWith('https://')));
for (const link of links) assert.ok(allowed.has(new URL(link).href), `Displayed link is blocked by native policy: ${link}`);
console.log(`PASS ${links.size} displayed links and ${sources.length} live document sources match the native allowlist; embedded fallbacks match their source.`);
