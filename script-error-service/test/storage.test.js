import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { test } from "node:test";
import { DatabaseSync } from "node:sqlite";

import {
  fingerprintReport,
  publicCodeCandidates,
  validateAndNormalizeReport,
} from "../src/normalization.js";
import {
  findPublicError,
  findTopErrors,
  recordOccurrence,
} from "../src/storage.js";

const MIGRATIONS_URL = new URL("../migrations/", import.meta.url);
const HOUR = 3600;
const NOW = new Date("2026-11-06T12:30:00Z");

class Statement {
  constructor(database, sql, params = []) {
    this.database = database;
    this.sql = sql;
    this.params = params;
  }

  bind(...params) {
    return new Statement(this.database, this.sql, params);
  }

  async first() {
    const row = this.database.prepare(this.sql).get(...this.params);
    return row === undefined ? null : { ...row };
  }

  async all() {
    return { results: this.database.prepare(this.sql).all(...this.params).map((row) => ({ ...row })) };
  }

  execute() {
    const statement = this.database.prepare(this.sql);
    if (/^\s*SELECT/i.test(this.sql)) {
      return { results: statement.all(...this.params).map((row) => ({ ...row })), meta: {} };
    }
    const { changes } = statement.run(...this.params);
    return { results: [], meta: { changes } };
  }
}

/** The slice of the D1 API storage.js uses, over a real SQLite database. */
function d1(database) {
  return {
    prepare: (sql) => new Statement(database, sql),
    async batch(statements) {
      database.exec("BEGIN");
      try {
        const results = statements.map((statement) => statement.execute());
        database.exec("COMMIT");
        return results;
      } catch (error) {
        database.exec("ROLLBACK");
        throw error;
      }
    },
  };
}

function migrationNames() {
  return readdirSync(MIGRATIONS_URL).filter((name) => name.endsWith(".sql")).sort();
}

function migratedDatabase(upTo = Infinity) {
  const database = new DatabaseSync(":memory:");
  for (const name of migrationNames().slice(0, upTo)) {
    database.exec(readFileSync(new URL(name, MIGRATIONS_URL), "utf8"));
  }
  return database;
}

function frame(overrides = {}) {
  return {
    function: "ClientHudInit",
    source: "scripts/vscripts/client/cl_main_hud.nut",
    line: 4276,
    modded: false,
    ...overrides,
  };
}

function schema1(overrides = {}) {
  return validateAndNormalizeReport({
    schema: 1,
    vmContext: "CLIENT",
    processMode: "CLIENT",
    cascade: false,
    error: "the index 'x' does not exist",
    frames: [{ function: "ClientHudInit", source: frame().source, line: 4276 }],
    ...overrides,
  });
}

function schema2(overrides = {}) {
  return validateAndNormalizeReport({
    schema: 2,
    vmContext: "CLIENT",
    processMode: "CLIENT",
    cascade: false,
    error: "the index 'x' does not exist",
    frames: [frame()],
    modded: [],
    ...overrides,
  });
}

async function record(db, report, now = NOW) {
  const fingerprint = await fingerprintReport(report);
  const result = await recordOccurrence(db, report, fingerprint, publicCodeCandidates(fingerprint), now);
  return { fingerprint, ...result };
}

test("migration 0004 adds zeroed counters to rows recorded before it", async () => {
  const database = migratedDatabase(3);
  const db = d1(database);
  // A row exactly as the previous schema stored it.
  database
    .prepare(
      `INSERT INTO errors (fingerprint, code, schema_version, vm_context, error_text,
         frames_json, first_seen, last_seen, total_count, cascade_count)
       VALUES (?, 'ABCDEFGH', 1, 'CLIENT', 'old', '[]', 1, 1, 5, 2)`,
    )
    .run("a".repeat(64));
  database
    .prepare(
      `INSERT INTO error_buckets (fingerprint, bucket_start, process_mode, vm_context, count, cascade_count)
       VALUES (?, 3600, 'CLIENT', 'CLIENT', 5, 2)`,
    )
    .run("a".repeat(64));

  database.exec(readFileSync(new URL("0004_modded_counts.sql", MIGRATIONS_URL), "utf8"));

  const error = await db.prepare("SELECT classified_count AS c, modded_count AS m FROM errors").first();
  const bucket = await db.prepare("SELECT classified_count AS c, modded_count AS m FROM error_buckets").first();
  assert.deepEqual(error, { c: 0, m: 0 });
  assert.deepEqual(bucket, { c: 0, m: 0 });
  assert.throws(() => database.exec("UPDATE errors SET modded_count = 1"), /CHECK/);
});

test("occurrences count classified and modded games per error and per hour bucket", async () => {
  const db = d1(migratedDatabase());
  const first = await record(db, schema1());
  assert.equal(first.isNew, true);

  // The same error from a schema 2 client in a stock game and in a modded game (frames unmodded).
  const stock = await record(db, schema2());
  const moddedGame = await record(db, schema2({ modded: ["addons"] }));
  assert.equal(stock.code, first.code, "an unmodded schema 2 report shares the schema 1 code");
  assert.equal(moddedGame.code, first.code);
  assert.equal(moddedGame.isNew, false);

  // Next hour: another modded occurrence.
  await record(db, schema2({ modded: ["core", "native"] }), new Date(NOW.getTime() + HOUR * 1000));

  const error = await db.prepare("SELECT total_count, classified_count, modded_count FROM errors").first();
  assert.deepEqual(error, { total_count: 4, classified_count: 3, modded_count: 2 });

  const { results: buckets } = await db
    .prepare("SELECT bucket_start, count, classified_count, modded_count FROM error_buckets ORDER BY bucket_start")
    .all();
  const firstBucket = Math.floor(NOW.getTime() / 1000 / HOUR) * HOUR;
  assert.deepEqual(buckets, [
    { bucket_start: firstBucket, count: 3, classified_count: 2, modded_count: 1 },
    { bucket_start: firstBucket + HOUR, count: 1, classified_count: 1, modded_count: 1 },
  ]);
});

test("a modded frame is a separate error whose stored frames carry the flag", async () => {
  const db = d1(migratedDatabase());
  const stock = await record(db, schema2());
  const modded = await record(db, schema2({ frames: [frame({ modded: true })], modded: ["addons"] }));
  assert.notEqual(stock.code, modded.code);
  assert.equal(modded.isNew, true);

  const stockPublic = await findPublicError(db, stock.code);
  const moddedPublic = await findPublicError(db, modded.code);
  assert.equal(stockPublic.frames[0].modded, undefined);
  assert.equal(moddedPublic.frames[0].modded, true);
  assert.equal(moddedPublic.moddedCount, 1);
  assert.equal(moddedPublic.classifiedCount, 1);
  assert.equal(stockPublic.moddedCount, 0);
});

test("admin ranking reports the modded share and filters modded games", async () => {
  const db = d1(migratedDatabase());
  // stock error: 2 unclassified + 1 stock + 1 modded.
  await record(db, schema1());
  await record(db, schema1());
  await record(db, schema2());
  const shared = await record(db, schema2({ modded: ["addons"] }));
  // mod-only error: 3 modded occurrences, modded frame.
  const modOnly = schema2({
    error: "from a mod",
    frames: [frame({ source: "scripts/vscripts/r1bots/_r1bots.nut", modded: true })],
    modded: ["addons"],
  });
  for (let index = 0; index < 3; index += 1) {
    await record(db, modOnly);
  }

  const cutoff = Math.floor(NOW.getTime() / 1000 / HOUR) * HOUR - HOUR;
  const all = await findTopErrors(db, cutoff, 10, "all");
  assert.deepEqual(
    all.map((row) => [row.code === shared.code, row.windowCount, row.windowClassifiedCount, row.windowModdedCount]),
    [
      [true, 4, 2, 1],
      [false, 3, 3, 3],
    ],
  );
  assert.equal(all[0].moddedShare, 0.5);
  assert.equal(all[1].moddedShare, 1);

  const excluded = await findTopErrors(db, cutoff, 10, "exclude");
  assert.deepEqual(excluded.map((row) => row.windowCount), [3], "the mod-only error disappears");
  assert.equal(excluded[0].code, shared.code);

  const only = await findTopErrors(db, cutoff, 10, "only");
  assert.deepEqual(only.map((row) => row.windowCount), [3, 1]);

  await assert.rejects(findTopErrors(db, cutoff, 10, "1; DROP TABLE errors"), TypeError);
});

test("a ranking over unclassified reports has no modded share", async () => {
  const db = d1(migratedDatabase());
  await record(db, schema1());
  const [row] = await findTopErrors(db, 0, 10, "all");
  assert.equal(row.moddedShare, null);
  assert.equal(row.windowClassifiedCount, 0);
});
