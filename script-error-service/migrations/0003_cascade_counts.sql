ALTER TABLE errors
ADD COLUMN cascade_count INTEGER NOT NULL DEFAULT 0
CHECK (cascade_count >= 0);

ALTER TABLE error_buckets
ADD COLUMN cascade_count INTEGER NOT NULL DEFAULT 0
CHECK (cascade_count >= 0);
