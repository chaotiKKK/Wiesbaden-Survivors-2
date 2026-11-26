#!/usr/bin/env node
/**
 * Phase 0 data extraction.
 *
 * Reads the untouched source game (F:/wiesbaden-survivors-apk/www/index.html),
 * slices the plain-JS data declarations and evaluates them in a sandbox with the
 * original helper shorthands (W, E, I) so the numbers stay byte-identical to the
 * shipping game. Emits:
 *   Content/Data/*.json  -> loaded by WS2Core at runtime
 *   Data/*.csv           -> reference/import tables (DataTable-ready)
 *   Data/extract-report.json -> counts used for the acceptance check
 */
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";

const SOURCE = "F:/wiesbaden-survivors-apk/www/index.html";
const ROOT = path.resolve(import.meta.dirname, "..");
const JSON_DIR = path.join(ROOT, "Content", "Data");
const CSV_DIR = path.join(ROOT, "Data");

const html = fs.readFileSync(SOURCE, "utf8");

function sliceArray(name) {
  const re = new RegExp(`const ${name} = \\[([\\s\\S]*?)\\n\\];`);
  const m = html.match(re);
  if (!m) throw new Error(`array not found: ${name}`);
  return `[${m[1]}\n]`;
}

function sliceObject(name) {
  const re = new RegExp(`const ${name} = \\{([\\s\\S]*?)\\n\\};`);
  const m = html.match(re);
  if (!m) throw new Error(`object not found: ${name}`);
  return `({${m[1]}\n})`;
}

function evalLiteral(src, name) {
  const helpers = `
    const W = (o) => o;
    const E = (id, name, o) => Object.assign({ id, name }, o || {});
    const I = (id, name, r, price, stats, fl, extra) => Object.assign({ id, name, r, price, stats, fl }, extra || {});
    const T = (v) => v;
    module.exports = ${src};
  `;
  const sandbox = { module: { exports: null }, Math, Object, Array, String, Number, JSON, console, undefined };
  vm.createContext(sandbox);
  vm.runInContext(helpers, sandbox, { timeout: 5000, filename: `${name}.js` });
  return sandbox.module.exports;
}

const WEAPONS = evalLiteral(sliceArray("WEAPONS"), "WEAPONS");
const ITEMS = evalLiteral(sliceArray("ITEMS"), "ITEMS");
const ENEMIES = evalLiteral(sliceArray("ENEMIES"), "ENEMIES");
const BOSSES = evalLiteral(sliceArray("BOSSES"), "BOSSES");
const ARENAS = evalLiteral(sliceArray("ARENAS"), "ARENAS");
const CHARS = evalLiteral(sliceArray("CHARS"), "CHARS");

const STAT_DEF = evalLiteral(sliceArray("STAT_DEF"), "STAT_DEF");
const ELEMENTS = evalLiteral(sliceObject("ELEMENTS"), "ELEMENTS");

const csvEscape = (v) => {
  if (v === undefined || v === null) return "";
  if (typeof v === "object") v = JSON.stringify(v);
  const s = String(v);
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
};

function writeCsv(file, columns, rows) {
  const lines = [columns.join(",")];
  for (const row of rows) lines.push(columns.map((c) => csvEscape(row[c])).join(","));
  fs.writeFileSync(path.join(CSV_DIR, file), lines.join("\n") + "\n", "utf8");
}

function writeJson(file, data) {
  fs.writeFileSync(path.join(JSON_DIR, file), JSON.stringify(data, null, 2) + "\n", "utf8");
}

fs.mkdirSync(JSON_DIR, { recursive: true });
fs.mkdirSync(CSV_DIR, { recursive: true });

// ---------------------------------------------------------------- weapons
const weaponRows = [];
const weapons = WEAPONS.map((w) => {
  const tiers = (w.tiers || []).map((t, i) => {
    const [damage, cooldown, speed, crit, critDmg, price, extras] = t;
    const ex = extras || {};
    // The source encodes elemental identity in the tier extras.  Keep that
    // mapping here so the runtime only has to read a single element id.
    const element = ex.burn ? "fire"
      : ex.slow ? "ice"
        : (ex.shock || ex.chain) ? "shock"
          : (ex.poison || ex.cloud) ? "poison"
            : ex.boom ? "boom" : "";
    return { tier: i, damage, cooldown, speed, crit, critDmg, price, element, extras: ex };
  });
  tiers.forEach((t) => {
    weaponRows.push({
      id: w.id,
      name: w.name,
      cls: (w.cls || []).join("|"),
      type: w.type,
      tier: t.tier,
      damage: t.damage,
      cooldown: t.cooldown,
      speed: t.speed,
      crit: t.crit,
      critdmg: t.critDmg,
      price: t.price,
      element: t.element,
      extras: t.extras,
      special: w.special
    });
  });
  return {
    id: w.id,
    name: w.name,
    classes: w.cls || [],
    type: w.type,
    color: w.col,
    scaling: w.scaling || {},
    unlockDefault: !!w.unlockDefault,
    unlock: w.unlock || null,
    special: w.special || "",
    tiers
  };
});
writeJson("weapons.json", weapons);
writeCsv("weapons.csv", ["id", "name", "cls", "type", "tier", "damage", "cooldown", "speed", "crit", "critdmg", "price", "element", "extras", "special"], weaponRows);

// ---------------------------------------------------------------- characters
writeJson("characters.json", CHARS);
writeCsv(
  "characters.csv",
  ["id", "name", "role", "desc", "stats", "startWeapon", "ability", "synergy", "unlock", "skin"],
  CHARS.map((c) => ({
    id: c.id,
    name: c.name,
    role: c.role,
    desc: c.desc,
    stats: c.stats,
    startWeapon: c.startWeapon,
    ability: c.ability,
    synergy: c.synergy,
    unlock: c.unlock,
    skin: c.skin
  }))
);

// ---------------------------------------------------------------- items / enemies / bosses / arenas
writeJson("items.json", ITEMS);
writeCsv("items.csv", ["id", "name", "r", "price", "stats", "fl"], ITEMS);

writeJson("enemies.json", ENEMIES);
writeCsv(
  "enemies.csv",
  ["id", "name", "hp", "dmg", "spd", "r", "col", "xp", "mat", "armor", "ai", "fly", "shape", "minW", "w", "pack"],
  ENEMIES.map((e) => ({
    id: e.id,
    name: e.name,
    hp: e.hp,
    dmg: e.dmg,
    spd: e.spd,
    r: e.r,
    col: e.col,
    xp: e.xp,
    mat: e.mat,
    armor: e.armor,
    ai: e.ai,
    fly: e.fly,
    shape: e.shape,
    minW: e.minW,
    w: e.w,
    pack: e.pack
  }))
);

writeJson("bosses.json", BOSSES);
writeCsv(
  "bosses.csv",
  ["id", "name", "arena", "hp", "r", "col", "spd", "dmg", "armor", "mat", "xp", "phases"],
  BOSSES.map((b) => ({
    id: b.id,
    name: b.name,
    arena: b.arena,
    hp: b.hp,
    r: b.r,
    col: b.col,
    spd: b.spd,
    dmg: b.dmg,
    armor: b.armor,
    mat: b.mat,
    xp: b.xp,
    phases: b.phases
  }))
);

writeJson("arenas.json", ARENAS);
writeCsv(
  "arenas.csv",
  ["id", "name", "w", "h", "ground", "grid", "accent", "theme", "buildings", "cover", "poison", "speedField", "movingWalls", "desc"],
  ARENAS.map((a) => ({
    id: a.id,
    name: a.name,
    w: a.w,
    h: a.h,
    ground: a.ground,
    grid: a.grid,
    accent: a.accent,
    theme: a.theme,
    buildings: a.buildings,
    cover: a.cover,
    poison: a.poison,
    speedField: a.speedField,
    movingWalls: a.movingWalls,
    desc: a.desc
  }))
);

writeJson("stats.json", STAT_DEF.map(([id, name, unit]) => ({ id, name, unit })));

// ---------------------------------------------------------------- elements
const elements = Object.entries(ELEMENTS).map(([id, value]) => ({ id, ...value }));
writeJson("elements.json", elements);
writeCsv("elements.csv", ["id", "name", "color", "icon", "kind"], elements);

// ---------------------------------------------------------------- acceptance checks
// 24 stats: the source's own header comment says 23, the table carries 24 entries.
const expected = { weapons: 40, items: 44, enemies: 28, bosses: 8, arenas: 7, characters: 16, stats: 24, elements: 5 };
const actual = {
  weapons: weapons.length,
  items: ITEMS.length,
  enemies: ENEMIES.length,
  bosses: BOSSES.length,
  arenas: ARENAS.length,
  characters: CHARS.length,
  stats: STAT_DEF.length,
  elements: elements.length,
  weaponTiers: weapons.reduce((a, w) => a + w.tiers.length, 0)
};

const mismatches = Object.entries(expected).filter(([k, v]) => actual[k] !== v);
const report = { source: SOURCE, expected, actual, mismatches, extractedAt: new Date().toISOString() };
fs.writeFileSync(path.join(CSV_DIR, "extract-report.json"), JSON.stringify(report, null, 2) + "\n", "utf8");

console.log("extracted", JSON.stringify(actual));
if (mismatches.length) {
  console.error("MISMATCH", JSON.stringify(mismatches));
  process.exit(1);
}
console.log("all counts match the shipping game");
