PRAGMA foreign_keys = ON;

CREATE TABLE errors (
  fingerprint TEXT PRIMARY KEY
    CHECK (
      length(fingerprint) = 64
      AND fingerprint NOT GLOB '*[^0-9a-f]*'
    ),
  code TEXT NOT NULL COLLATE NOCASE UNIQUE
    CHECK (
      length(code) BETWEEN 8 AND 52
      AND code = upper(code)
      AND code NOT GLOB '*[^0-9ABCDEFGHJKMNPQRSTVWXYZ]*'
    ),
  schema_version INTEGER NOT NULL CHECK (schema_version = 1),
  vm_context TEXT NOT NULL CHECK (vm_context IN ('SERVER', 'CLIENT', 'UI')),
  error_text TEXT NOT NULL CHECK (length(error_text) > 0),
  frames_json TEXT NOT NULL
    CHECK (json_valid(frames_json) AND json_type(frames_json) = 'array'),
  first_seen INTEGER NOT NULL CHECK (first_seen >= 0),
  last_seen INTEGER NOT NULL CHECK (last_seen >= first_seen),
  total_count INTEGER NOT NULL CHECK (total_count >= 0)
) STRICT;

CREATE TABLE error_buckets (
  fingerprint TEXT NOT NULL,
  bucket_start INTEGER NOT NULL
    CHECK (bucket_start >= 0 AND bucket_start % 3600 = 0),
  process_mode TEXT NOT NULL
    CHECK (process_mode IN ('client', 'dedicated', 'r1o_dedicated')),
  vm_context TEXT NOT NULL CHECK (vm_context IN ('SERVER', 'CLIENT', 'UI')),
  count INTEGER NOT NULL CHECK (count > 0),
  PRIMARY KEY (fingerprint, bucket_start, process_mode, vm_context),
  FOREIGN KEY (fingerprint) REFERENCES errors(fingerprint) ON DELETE CASCADE
) STRICT, WITHOUT ROWID;

CREATE INDEX error_buckets_by_window
  ON error_buckets(bucket_start, fingerprint, process_mode, vm_context);

CREATE INDEX errors_by_last_seen
  ON errors(last_seen DESC);
