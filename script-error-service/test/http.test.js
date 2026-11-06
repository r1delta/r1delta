import assert from "node:assert/strict";
import { test } from "node:test";

import worker from "../src/index.js";
import { MAX_REQUEST_BYTES } from "../src/normalization.js";

async function errorResponse(request, env = {}) {
  const response = await worker.fetch(request, env);
  return { response, body: await response.json() };
}

test("known endpoints reject unsupported methods", async () => {
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/reports"),
  );

  assert.equal(response.status, 405);
  assert.equal(response.headers.get("allow"), "POST");
  assert.equal(body.error.code, "method_not_allowed");
});

test("report endpoint rejects loose media types before reading a body", async () => {
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/reports", {
      method: "POST",
      headers: { "Content-Type": "application/problem+json" },
      body: "{}",
    }),
  );

  assert.equal(response.status, 415);
  assert.equal(body.error.code, "unsupported_media_type");
});

test("declared bodies over 16 KiB are rejected before rate limiting", async () => {
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/reports", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Content-Length": String(MAX_REQUEST_BYTES + 1),
      },
      body: "{}",
    }),
  );

  assert.equal(response.status, 413);
  assert.equal(body.error.code, "body_too_large");
});

test("invalid reports do not consume rate-limit quota", async () => {
  let rateLimitCalls = 0;
  const env = {
    REPORT_RATE_LIMIT: {
      async limit() {
        rateLimitCalls += 1;
        return { success: true };
      },
    },
  };

  const malformed = await errorResponse(
    new Request("https://errors.example.test/v1/reports", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{",
    }),
    env,
  );
  assert.equal(malformed.response.status, 400);
  assert.equal(malformed.body.error.code, "invalid_json");

  const invalidSchema = await errorResponse(
    new Request("https://errors.example.test/v1/reports", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    }),
    env,
  );
  assert.equal(invalidSchema.response.status, 400);
  assert.equal(invalidSchema.body.error.code, "invalid_report");
  assert.equal(rateLimitCalls, 0);
});

test("validated reports consume rate-limit quota before storage", async () => {
  let rateLimitCalls = 0;
  let rateLimitKey;
  const env = {
    REPORT_RATE_LIMIT: {
      async limit({ key }) {
        rateLimitCalls += 1;
        rateLimitKey = key;
        return { success: false };
      },
    },
  };
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/reports", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        schema: 1,
        vmContext: "CLIENT",
        processMode: "CLIENT",
        cascade: false,
        error: "missing function",
        frames: [],
      }),
    }),
    env,
  );

  assert.equal(response.status, 429);
  assert.equal(body.error.code, "rate_limited");
  assert.equal(rateLimitCalls, 1);
  assert.equal(rateLimitKey, "report:missing");
});

test("public lookups use a separate per-IP rate-limit bucket", async () => {
  const keys = [];
  const env = {
    REPORT_RATE_LIMIT: {
      async limit({ key }) {
        keys.push(key);
        return { success: false };
      },
    },
  };
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/errors/01234567", {
      headers: { "CF-Connecting-IP": "203.0.113.7" },
    }),
    env,
  );

  assert.equal(response.status, 429);
  assert.equal(body.error.code, "rate_limited");
  assert.deepEqual(keys, ["lookup:203.0.113.7"]);
});

test("admin routes fail closed when no secret binding is configured", async () => {
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/v1/admin/top"),
  );

  assert.equal(response.status, 503);
  assert.equal(body.error.code, "admin_auth_unavailable");
  assert.equal(response.headers.get("cache-control"), "no-store");
});

function databaseStub(overrides = {}) {
  const calls = [];
  return {
    calls,
    prepare(sql) {
      return {
        bind(...params) {
          calls.push({ sql, params });
          return {
            sql,
            params,
            async first() {
              return overrides.first ?? null;
            },
            async all() {
              return { results: overrides.rows ?? [] };
            },
          };
        },
      };
    },
    async batch(statements) {
      calls.push({ batch: statements.map((statement) => statement.params) });
      return [{ meta: { changes: 1 } }, {}, {}, { results: [{ code: "ABCDEFGH", totalCount: 1 }] }];
    },
  };
}

test("health advertises the accepted report schemas", async () => {
  const { response, body } = await errorResponse(
    new Request("https://errors.example.test/health"),
    { DB: databaseStub({ first: { healthy: 2 } }) },
  );

  assert.equal(response.status, 200);
  assert.deepEqual(body, { status: "ok", schemas: [1, 2] });
});

function postReportRequest(body) {
  return new Request("https://errors.example.test/v1/reports", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

const allowingRateLimit = {
  async limit() {
    return { success: true };
  },
};

test("schema 1 and schema 2 reports are both accepted", async () => {
  const base = {
    vmContext: "CLIENT",
    processMode: "CLIENT",
    cascade: false,
    error: "missing function",
  };
  const db = databaseStub();
  const env = { DB: db, REPORT_RATE_LIMIT: allowingRateLimit };

  const legacy = await errorResponse(
    postReportRequest({ ...base, schema: 1, frames: [] }),
    env,
  );
  const modded = await errorResponse(
    postReportRequest({
      ...base,
      schema: 2,
      modded: ["addons"],
      frames: [{ function: "f", source: "scripts/vscripts/x.nut", line: 3, modded: true }],
    }),
    env,
  );
  const invalid = await errorResponse(
    postReportRequest({ ...base, schema: 2, frames: [] }),
    env,
  );

  assert.equal(legacy.response.status, 200);
  assert.equal(modded.response.status, 200);
  assert.equal(invalid.response.status, 400);
  assert.equal(invalid.body.error.code, "invalid_report");

  // Statement parameters: [... cascade, classified, modded, fingerprint] for the error update.
  const updates = db.calls
    .filter((call) => call.batch)
    .map((call) => call.batch[1]);
  assert.deepEqual(updates[0].slice(1, 4), [0, 0, 0], "schema 1 is unclassified");
  assert.deepEqual(updates[1].slice(1, 4), [0, 1, 1], "schema 2 modded game is counted");
});

test("admin ranking validates and applies the modded filter", async () => {
  const request = (query) =>
    new Request(`https://errors.example.test/v1/admin/top${query}`, {
      headers: { Authorization: "Bearer secret" },
    });
  const db = databaseStub();
  const env = { DB: db, ADMIN_TOKEN: "secret" };

  const invalid = await errorResponse(request("?modded=maybe"), env);
  assert.equal(invalid.response.status, 400);
  assert.equal(invalid.body.error.code, "invalid_query");

  const only = await errorResponse(request("?modded=only"), env);
  assert.equal(only.response.status, 200);
  assert.equal(only.body.modded, "only");
  assert.match(db.calls.at(-1).sql, /SUM\(b\.modded_count\) AS windowCount/);

  const defaulted = await errorResponse(request(""), env);
  assert.equal(defaulted.body.modded, "all");
  assert.match(db.calls.at(-1).sql, /SUM\(b\.count\) AS windowCount/);
});
