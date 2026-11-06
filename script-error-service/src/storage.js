import { isClassified, isModdedGame } from "./normalization.js";

const ONE_HOUR_SECONDS = 60 * 60;

/**
 * Which occurrences a ranking counts. Schema 1 occurrences are unclassified (the client could not
 * tell), so "exclude" keeps them: it drops only occurrences known to come from modded games.
 */
export const MODDED_FILTERS = Object.freeze(["all", "exclude", "only"]);

const WINDOW_COUNT_SQL = Object.freeze({
  all: "SUM(b.count)",
  exclude: "SUM(b.count - b.modded_count)",
  only: "SUM(b.modded_count)",
});

function unixSeconds(now) {
  return Math.floor(now.getTime() / 1000);
}

function bucketStart(nowSeconds) {
  return Math.floor(nowSeconds / ONE_HOUR_SECONDS) * ONE_HOUR_SECONDS;
}

function firstResult(batchEntry) {
  return batchEntry?.results?.[0] ?? null;
}

/**
 * Records one aggregate occurrence. D1 batch() is transactional, so a short-code
 * collision either creates no report row or records the complete occurrence.
 *
 * errors.schema_version stays 1 ("stored layout 1"): schema 2 only adds an optional
 * `modded: true` member to stored frames, which readers of layout 1 ignore.
 */
export async function recordOccurrence(
  db,
  report,
  fingerprint,
  codeCandidates,
  now = new Date(),
) {
  const observedAt = unixSeconds(now);
  const observedBucket = bucketStart(observedAt);
  const framesJson = JSON.stringify(report.frames);
  const cascade = report.cascade ? 1 : 0;
  const classified = isClassified(report) ? 1 : 0;
  const modded = isModdedGame(report) ? 1 : 0;

  for (const code of codeCandidates) {
    const insertError = db
      .prepare(
        `INSERT INTO errors (
           fingerprint, code, schema_version, vm_context, error_text,
           frames_json, first_seen, last_seen, total_count, cascade_count,
           classified_count, modded_count
         ) VALUES (?, ?, 1, ?, ?, ?, ?, ?, 0, 0, 0, 0)
         ON CONFLICT DO NOTHING`,
      )
      .bind(
        fingerprint,
        code,
        report.vmContext,
        report.error,
        framesJson,
        observedAt,
        observedAt,
      );

    const incrementError = db
      .prepare(
        `UPDATE errors
         SET last_seen = MAX(last_seen, ?),
             total_count = total_count + 1,
             cascade_count = cascade_count + ?,
             classified_count = classified_count + ?,
             modded_count = modded_count + ?
         WHERE fingerprint = ?`,
      )
      .bind(observedAt, cascade, classified, modded, fingerprint);

    const incrementBucket = db
      .prepare(
        `INSERT INTO error_buckets (
           fingerprint, bucket_start, process_mode, vm_context, count,
           cascade_count, classified_count, modded_count
         )
         SELECT ?, ?, ?, ?, 1, ?, ?, ?
         WHERE EXISTS (
           SELECT 1 FROM errors WHERE fingerprint = ?
         )
         ON CONFLICT (fingerprint, bucket_start, process_mode, vm_context)
         DO UPDATE SET
           count = count + 1,
           cascade_count = cascade_count + excluded.cascade_count,
           classified_count = classified_count + excluded.classified_count,
           modded_count = modded_count + excluded.modded_count`,
      )
      .bind(
        fingerprint,
        observedBucket,
        report.processMode,
        report.vmContext,
        cascade,
        classified,
        modded,
        fingerprint,
      );

    const selectStored = db
      .prepare(
        `SELECT code, total_count AS totalCount
         FROM errors
         WHERE fingerprint = ?`,
      )
      .bind(fingerprint);

    const results = await db.batch([
      insertError,
      incrementError,
      incrementBucket,
      selectStored,
    ]);
    const stored = firstResult(results[3]);

    if (stored) {
      return {
        code: stored.code,
        isNew: results[0]?.meta?.changes === 1,
      };
    }
  }

  throw new Error("SHA-256 public-code space exhausted");
}

function parseFrames(framesJson) {
  const frames = JSON.parse(framesJson);
  if (!Array.isArray(frames)) {
    throw new Error("Stored frame data is invalid");
  }
  return frames;
}

function isoTime(unixTime) {
  return new Date(unixTime * 1000).toISOString();
}

/** Share of classified occurrences that came from modded games; null while none are classified. */
function moddedShare(moddedCount, classifiedCount) {
  return classifiedCount > 0 ? moddedCount / classifiedCount : null;
}

export async function findPublicError(db, code) {
  const row = await db
    .prepare(
      `SELECT
         code,
         vm_context AS vmContext,
         error_text AS error,
         frames_json AS framesJson,
         first_seen AS firstSeen,
         last_seen AS lastSeen,
         total_count AS totalCount,
         cascade_count AS cascadeCount,
         classified_count AS classifiedCount,
         modded_count AS moddedCount
       FROM errors
       WHERE code = ?`,
    )
    .bind(code)
    .first();

  if (!row) {
    return null;
  }

  const { results: processTypes } = await db
    .prepare(
      `SELECT process_mode AS processType, SUM(count) AS count
       FROM error_buckets
       WHERE fingerprint = (SELECT fingerprint FROM errors WHERE code = ?)
       GROUP BY process_mode
       ORDER BY count DESC, processType ASC`,
    )
    .bind(code)
    .all();

  return {
    schema: 1,
    code: row.code,
    vmContext: row.vmContext,
    processTypes,
    cascadeCount: row.cascadeCount,
    classifiedCount: row.classifiedCount,
    moddedCount: row.moddedCount,
    error: row.error,
    frames: parseFrames(row.framesJson),
    firstSeen: isoTime(row.firstSeen),
    lastSeen: isoTime(row.lastSeen),
    totalCount: row.totalCount,
  };
}

export async function findTopErrors(db, cutoff, limit, moddedFilter = "all") {
  if (!Object.hasOwn(WINDOW_COUNT_SQL, moddedFilter)) {
    throw new TypeError("moddedFilter must be one of all, exclude, only");
  }

  const { results } = await db
    .prepare(
      `SELECT
         e.code AS code,
         b.vm_context AS vmContext,
         b.process_mode AS processMode,
         e.error_text AS error,
         e.first_seen AS firstSeen,
         e.last_seen AS lastSeen,
         e.total_count AS totalCount,
         e.cascade_count AS cascadeCount,
         e.classified_count AS classifiedCount,
         e.modded_count AS moddedCount,
         ${WINDOW_COUNT_SQL[moddedFilter]} AS windowCount,
         SUM(b.cascade_count) AS windowCascadeCount,
         SUM(b.classified_count) AS windowClassifiedCount,
         SUM(b.modded_count) AS windowModdedCount
       FROM error_buckets AS b
       INNER JOIN errors AS e ON e.fingerprint = b.fingerprint
       WHERE b.bucket_start >= ?
       GROUP BY
         e.fingerprint,
         e.code,
         b.vm_context,
         b.process_mode,
         e.error_text,
         e.first_seen,
         e.last_seen,
         e.total_count,
         e.cascade_count,
         e.classified_count,
         e.modded_count
       HAVING windowCount > 0
       ORDER BY windowCount DESC, e.last_seen DESC, e.code ASC
       LIMIT ?`,
    )
    .bind(cutoff, limit)
    .all();

  return results.map((row) => ({
    code: row.code,
    vmContext: row.vmContext,
    processMode: row.processMode,
    error: row.error,
    firstSeen: isoTime(row.firstSeen),
    lastSeen: isoTime(row.lastSeen),
    totalCount: row.totalCount,
    cascadeCount: row.cascadeCount,
    classifiedCount: row.classifiedCount,
    moddedCount: row.moddedCount,
    windowCount: row.windowCount,
    windowCascadeCount: row.windowCascadeCount,
    windowClassifiedCount: row.windowClassifiedCount,
    windowModdedCount: row.windowModdedCount,
    moddedShare: moddedShare(row.windowModdedCount, row.windowClassifiedCount),
  }));
}

export async function checkDatabase(db) {
  const row = await db
    .prepare(
      `SELECT COUNT(*) AS healthy
       FROM sqlite_master
       WHERE type = 'table' AND name IN (?, ?)`,
    )
    .bind("errors", "error_buckets")
    .first();
  return row?.healthy === 2;
}
