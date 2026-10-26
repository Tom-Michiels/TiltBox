CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  ts INTEGER NOT NULL,
  device TEXT,
  firmware TEXT,
  battery_mv INTEGER,
  audio_key TEXT,
  audio_bytes INTEGER,
  audio_seconds REAL,
  lang TEXT,
  stt_model TEXT,
  stt_ms INTEGER,
  transcript TEXT,
  gen_model TEXT,
  gen_ms INTEGER,
  prompt_version TEXT,
  code TEXT,
  code_chars INTEGER,
  finish_reason TEXT,
  usage_json TEXT,
  cost_usd REAL,
  error TEXT,
  result_status TEXT,
  result_ms INTEGER,
  result_output TEXT,
  result_ts INTEGER
);
CREATE INDEX IF NOT EXISTS sessions_ts ON sessions(ts DESC);

-- Offline experiments (replay tool): same transcript, other model/prompt.
CREATE TABLE IF NOT EXISTS replays (
  id TEXT PRIMARY KEY,
  session_id TEXT,
  ts INTEGER NOT NULL,
  gen_model TEXT,
  prompt_version TEXT,
  gen_ms INTEGER,
  code TEXT,
  finish_reason TEXT,
  usage_json TEXT,
  cost_usd REAL,
  host_status TEXT,
  host_output TEXT,
  note TEXT
);
