const ONE_HOUR_SECONDS = 60 * 60;

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

  for (const code of codeCandidates) {
    const insertError = db
      .prepare(
        `INSERT INTO errors (
           fingerprint, code, schema_version, vm_context, error_text,
           frames_json, first_seen, last_seen, total_count, cascade_count
         ) VALUES (?, ?, 1, ?, ?, ?, ?, ?, 0, 0)
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
             cascade_count = cascade_count + ?
         WHERE fingerprint = ?`,
      )
      .bind(observedAt, report.cascade ? 1 : 0, fingerprint);

    const incrementBucket = db
      .prepare(
        `INSERT INTO error_buckets (
           fingerprint, bucket_start, process_mode, vm_context, count,
           cascade_count
         )
         SELECT ?, ?, ?, ?, 1, ?
         WHERE EXISTS (
           SELECT 1 FROM errors WHERE fingerprint = ?
         )
         ON CONFLICT (fingerprint, bucket_start, process_mode, vm_context)
         DO UPDATE SET
           count = count + 1,
           cascade_count = cascade_count + excluded.cascade_count`,
      )
      .bind(
        fingerprint,
        observedBucket,
        report.processMode,
        report.vmContext,
        report.cascade ? 1 : 0,
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
         cascade_count AS cascadeCount
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
    error: row.error,
    frames: parseFrames(row.framesJson),
    firstSeen: isoTime(row.firstSeen),
    lastSeen: isoTime(row.lastSeen),
    totalCount: row.totalCount,
  };
}

export async function findTopErrors(db, cutoff, limit) {
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
         SUM(b.count) AS windowCount,
         SUM(b.cascade_count) AS windowCascadeCount
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
         e.cascade_count
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
    windowCount: row.windowCount,
    windowCascadeCount: row.windowCascadeCount,
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
