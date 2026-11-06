-- Schema 2 reports carry a modded classification. Each occurrence is counted as classified
-- (the client could tell) and, when it came from a modded game, as modded. Occurrences recorded
-- before this migration and schema 1 reports are unclassified, so a modded share is always
-- modded_count / classified_count rather than a share of total_count.
ALTER TABLE errors
ADD COLUMN classified_count INTEGER NOT NULL DEFAULT 0
CHECK (classified_count >= 0);

ALTER TABLE errors
ADD COLUMN modded_count INTEGER NOT NULL DEFAULT 0
CHECK (modded_count >= 0 AND modded_count <= classified_count);

ALTER TABLE error_buckets
ADD COLUMN classified_count INTEGER NOT NULL DEFAULT 0
CHECK (classified_count >= 0);

ALTER TABLE error_buckets
ADD COLUMN modded_count INTEGER NOT NULL DEFAULT 0
CHECK (modded_count >= 0 AND modded_count <= classified_count);
