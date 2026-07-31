import assert from "node:assert/strict";
import { test } from "node:test";

import { isJsonContentType } from "../src/index.js";
import {
  constantTimeTokenEqual,
  isAuthorized,
  withSecurityHeaders,
} from "../src/security.js";

const expectedToken = ["test", "authorization", "value"].join("-");

function requestWithAuthorization(value) {
  return new Request("https://errors.example.test/v1/admin/top", {
    headers: value === undefined ? undefined : { Authorization: value },
  });
}

test("admin authorization accepts only an exact bearer token", async () => {
  assert.equal(
    await isAuthorized(
      requestWithAuthorization(`Bearer ${expectedToken}`),
      expectedToken,
    ),
    true,
  );
  assert.equal(
    await isAuthorized(
      requestWithAuthorization(`bearer ${expectedToken}`),
      expectedToken,
    ),
    true,
  );
  assert.equal(
    await isAuthorized(requestWithAuthorization("Bearer wrong-value"), expectedToken),
    false,
  );
  assert.equal(
    await isAuthorized(
      requestWithAuthorization(`Bearer ${expectedToken} trailing`),
      expectedToken,
    ),
    false,
  );
  assert.equal(await isAuthorized(requestWithAuthorization(), expectedToken), false);
  assert.equal(
    await isAuthorized(requestWithAuthorization(`Bearer ${expectedToken}`), undefined),
    false,
  );
});

test("token equality compares fixed-size digests", async () => {
  assert.equal(await constantTimeTokenEqual(expectedToken, expectedToken), true);
  assert.equal(await constantTimeTokenEqual("near-match", "near-matcg"), false);
  assert.equal(await constantTimeTokenEqual("short", "a-longer-token"), false);
  assert.equal(await constantTimeTokenEqual("", ""), false);
});

test("report content type is strict", () => {
  assert.equal(isJsonContentType("application/json"), true);
  assert.equal(isJsonContentType("Application/JSON; charset=UTF-8"), true);
  assert.equal(isJsonContentType('application/json; charset="utf-8"'), true);
  assert.equal(isJsonContentType("application/problem+json"), false);
  assert.equal(isJsonContentType("application/json; profile=test"), false);
  assert.equal(isJsonContentType("text/json"), false);
  assert.equal(isJsonContentType(null), false);
});

test("security headers deny active and cross-origin embedding", () => {
  const response = withSecurityHeaders(new Response("ok"), { html: true });

  assert.equal(response.headers.get("x-content-type-options"), "nosniff");
  assert.equal(response.headers.get("x-frame-options"), "DENY");
  assert.equal(response.headers.get("referrer-policy"), "no-referrer");
  assert.match(response.headers.get("content-security-policy"), /default-src 'none'/);
  assert.match(response.headers.get("content-security-policy"), /frame-ancestors 'none'/);
});
