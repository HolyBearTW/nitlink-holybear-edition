// Validate localization resources without a browser or third-party dependencies.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const context = vm.createContext({window: {}});
const locales = ['en-US', 'zh-TW', 'zh-CN', 'de-DE', 'ko-KR', 'ja-JP', 'tr-TR'];
for (const locale of locales) {
  vm.runInContext(fs.readFileSync(path.join(root, 'locales', locale + '.js'), 'utf8'), context);
}
const tables = context.window.NitLinkLocales;
const keys = Object.keys(tables['en-US']).sort();
const placeholders = value => [...value.matchAll(/\{\w+\}/g)].map(m => m[0]).sort();
for (const locale of locales) {
  assert.deepEqual(Object.keys(tables[locale]).sort(), keys, locale + ': missing/extra keys');
  for (const key of keys) {
    assert.equal(typeof tables[locale][key], 'string', locale + ': ' + key);
    assert.ok(tables[locale][key].trim(), locale + ': empty ' + key);
    assert.deepEqual(placeholders(tables[locale][key]), placeholders(tables['en-US'][key]), locale + ': ' + key);
  }
}
const html = fs.readFileSync(path.join(root, 'nitlink-menu.html'), 'utf8');
for (const [, key] of html.matchAll(/(?:data-i18n(?:-title|-aria-label)?|data-audio-help)="([^"]+)"/g)) {
  assert.ok(keys.includes(key), 'HTML references missing translation: ' + key);
}
const cpp = fs.readFileSync(path.join(root, 'src/app/localization.cpp'), 'utf8');
const nativeTables = [...cpp.matchAll(/const Table& (\w+)Table\(\)\s*\{([\s\S]*?)return table;/g)].map(([, name, body]) => {
  const values = new Map([...body.matchAll(/\{L"([^"]+)", L"((?:\\.|[^"\\])*)"\}/g)].map(m => [m[1], m[2]]));
  return {name, values};
});
assert.equal(nativeTables.length, locales.length);
for (const {name, values} of nativeTables) {
  assert.deepEqual([...values.keys()].sort(), [...nativeTables[0].values.keys()].sort(), name + ': native key coverage');
  for (const [key, value] of values) assert.deepEqual(placeholders(value), placeholders(nativeTables[0].values.get(key)), name + ': ' + key);
}
const installerDirectory = path.join(root, 'installer/winui/Localization');
const installerLanguages = JSON.parse(fs.readFileSync(path.join(installerDirectory, 'languages.json'), 'utf8'));
assert.deepEqual(installerLanguages.map(language => language.Id).sort(), [...locales].sort(), 'installer language coverage');
const installerEnglish = JSON.parse(fs.readFileSync(path.join(installerDirectory, 'en-US.json'), 'utf8'));
for (const locale of locales) {
  const catalog = JSON.parse(fs.readFileSync(path.join(installerDirectory, locale + '.json'), 'utf8'));
  assert.deepEqual(Object.keys(catalog).sort(), Object.keys(installerEnglish).sort(), locale + ': installer key coverage');
  for (const [key, value] of Object.entries(catalog)) {
    assert.equal(typeof value, 'string', locale + ': installer ' + key);
    assert.ok(value.trim(), locale + ': empty installer ' + key);
    assert.deepEqual(placeholders(value), placeholders(installerEnglish[key]), locale + ': installer ' + key);
  }
}
const nativeInstaller = fs.readFileSync(path.join(root, 'installer/strings.nsh'), 'utf8');
const installerTables = new Map();
for (const [, key, language, value] of nativeInstaller.matchAll(/^LangString (\w+) \$\{LANG_(\w+)\} "(.*)"\r?$/gm)) {
  if (!installerTables.has(language)) installerTables.set(language, new Map());
  assert.ok(!installerTables.get(language).has(key), language + ': duplicate installer ' + key);
  installerTables.get(language).set(key, value);
}
assert.deepEqual([...installerTables.keys()].sort(), ['ENGLISH', 'TRADCHINESE', 'SIMPCHINESE', 'GERMAN', 'JAPANESE', 'KOREAN', 'TURKISH'].sort());
const nativeInstallerEnglish = installerTables.get('ENGLISH');
for (const [language, table] of installerTables) {
  assert.deepEqual([...table.keys()].sort(), [...nativeInstallerEnglish.keys()].sort(), language + ': native installer key coverage');
  for (const [key, value] of table) {
    assert.ok(value.trim(), language + ': empty native installer ' + key);
    assert.deepEqual(placeholders(value), placeholders(nativeInstallerEnglish.get(key)), language + ': native installer ' + key);
  }
}
console.log(`Validated ${keys.length} menu, ${nativeTables[0].values.size} native, ${Object.keys(installerEnglish).length} WinUI installer and ${nativeInstallerEnglish.size} native installer strings in all ${locales.length} languages.`);
