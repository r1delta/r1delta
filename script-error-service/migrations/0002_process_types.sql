CREATE TABLE error_buckets_v2 (
  fingerprint TEXT NOT NULL,
  bucket_start INTEGER NOT NULL
    CHECK (bucket_start >= 0 AND bucket_start % 3600 = 0),
  process_mode TEXT NOT NULL
    CHECK (process_mode IN ('CLIENT', 'LISTEN', 'DEDICATED')),
  vm_context TEXT NOT NULL CHECK (vm_context IN ('SERVER', 'CLIENT', 'UI')),
  count INTEGER NOT NULL CHECK (count > 0),
  PRIMARY KEY (fingerprint, bucket_start, process_mode, vm_context),
  FOREIGN KEY (fingerprint) REFERENCES errors(fingerprint) ON DELETE CASCADE
) STRICT, WITHOUT ROWID;

INSERT INTO error_buckets_v2 (
  fingerprint, bucket_start, process_mode, vm_context, count
)
SELECT
  fingerprint,
  bucket_start,
  CASE
    WHEN process_mode = 'client' AND vm_context = 'SERVER' THEN 'LISTEN'
    WHEN process_mode = 'client' THEN 'CLIENT'
    ELSE 'DEDICATED'
  END,
  vm_context,
  SUM(count)
FROM error_buckets
GROUP BY
  fingerprint,
  bucket_start,
  CASE
    WHEN process_mode = 'client' AND vm_context = 'SERVER' THEN 'LISTEN'
    WHEN process_mode = 'client' THEN 'CLIENT'
    ELSE 'DEDICATED'
  END,
  vm_context;

DROP INDEX error_buckets_by_window;
DROP TABLE error_buckets;
ALTER TABLE error_buckets_v2 RENAME TO error_buckets;

CREATE INDEX error_buckets_by_window
  ON error_buckets(bucket_start, fingerprint, process_mode, vm_context);
