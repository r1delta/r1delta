import assert from "node:assert/strict";
import { test } from "node:test";

import {
  MAX_FRAMES,
  ValidationError,
  fingerprintReport,
  publicCodeCandidates,
  validateAndNormalizeReport,
} from "../src/normalization.js";

function report(overrides = {}) {
  return {
    schema: 1,
    vmContext: "SERVER",
    processMode: "DEDICATED",
    cascade: false,
    error: "Attempt to call missing function",
    frames: [
      {
        function: "RunMatch",
        source: "scripts/vscripts/match.nut",
        line: 42,
      },
    ],
    ...overrides,
  };
}

test("sanitization removes installation and identifying data", () => {
  const normalized = validateAndNormalizeReport(
    report({
      error:
        "Failure at \"C:\\Users\\Alice Smith\\game\\private.nut\" from 203.0.113.9 playerId=76561198012345678\nlocals: password=hunter2",
      frames: [
        {
          function: "Update deviceId=550e8400-e29b-41d4-a716-446655440000",
          source: "D:\\Games\\R1Delta\\scripts\\vscripts\\Modes\\Private.NUT",
          line: 7,
        },
      ],
    }),
  );

  assert.equal(
    normalized.error,
    'Failure at "<redacted>" from <ip> <id> <details redacted>',
  );
  assert.equal(normalized.frames[0].source, "modes/private.nut");
  assert.equal(normalized.frames[0].function, "Update <id>");
  assert.doesNotMatch(JSON.stringify(normalized), /Alice|203\.0\.113\.9|hunter2|7656119/);
});

test("all quoted error values are redacted without removing callstacks", () => {
  const normalized = validateAndNormalizeReport(
    report({
      error: "[CLIENT] the index 'PrivateModAccountLookup' does not exist",
      frames: [
        {
          function: "RunLookup",
          source: "scripts/vscripts/private/caller.nut",
          line: 37,
        },
      ],
    }),
  );
  const doubleQuoted = validateAndNormalizeReport(
    report({ error: 'Failure in "PrivateSecondLookup" with value \'PrivateValue\'' }),
  );

  assert.equal(
    normalized.error,
    "[CLIENT] the index '<redacted>' does not exist",
  );
  assert.deepEqual(normalized.frames, [
    { function: "RunLookup", source: "private/caller.nut", line: 37 },
  ]);
  assert.equal(
    doubleQuoted.error,
    'Failure in "<redacted>" with value \'<redacted>\'',
  );
  assert.doesNotMatch(
    JSON.stringify([normalized, doubleQuoted]),
    /PrivateMod|PrivateSecond|PrivateValue/,
  );
});

test("absolute source paths normalize to stable safe names", () => {
  const windows = validateAndNormalizeReport(
    report({
      frames: [
        {
          function: "Think",
          source: "C:\\one\\scripts\\vscripts\\boss.nut",
          line: 10,
        },
      ],
    }),
  );
  const posix = validateAndNormalizeReport(
    report({
      frames: [
        {
          function: "Think",
          source: "/srv/two/scripts/vscripts/boss.nut",
          line: 10,
        },
      ],
    }),
  );

  assert.equal(windows.frames[0].source, "boss.nut");
  assert.deepEqual(windows.frames, posix.frames);
});

test("unquoted paths with spaces and inline diagnostics fail closed", () => {
  const normalized = validateAndNormalizeReport(
    report({
      error:
        "Failure at C:\\Users\\Alice Smith\\private.nut from 203.0.113.9 locals: password=hunter2",
      frames: [
        {
          function: "Think locals: token=private",
          source: "/home/Alice Smith/game/scripts/vscripts/private.nut",
          line: 9,
        },
      ],
    }),
  );

  assert.match(normalized.error, /<path>/);
  assert.match(normalized.error, /<details redacted>/);
  assert.equal(normalized.frames[0].function, "Think <details redacted>");
  assert.equal(normalized.frames[0].source, "private.nut");
  assert.doesNotMatch(
    JSON.stringify(normalized),
    /Alice Smith|203\.0\.113\.9|hunter2|token=private/,
  );
});

test("validation accepts only the versioned report contract", () => {
  assert.equal(validateAndNormalizeReport(report()).schema, 1);

  assert.throws(
    () => validateAndNormalizeReport({ ...report(), accountId: "not-allowed" }),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ schema: 2 })),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ vmContext: "TOOLS" })),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ processMode: "r1o_dedicated" })),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ cascade: "yes" })),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ error: "  \n\t " })),
    ValidationError,
  );
  assert.throws(
    () =>
      validateAndNormalizeReport(
        report({
          frames: Array.from({ length: MAX_FRAMES + 1 }, () => ({
            function: "f",
            source: "x.nut",
            line: 1,
          })),
        }),
      ),
    ValidationError,
  );
  assert.throws(
    () =>
      validateAndNormalizeReport(
        report({ frames: [{ function: "f", source: "x.nut", line: -1 }] }),
      ),
    ValidationError,
  );
  assert.throws(
    () =>
      validateAndNormalizeReport(
        report({
          frames: [{ function: "f", source: "x.nut", line: 1, local: "x" }],
        }),
      ),
    ValidationError,
  );
});

test("fingerprints are stable and exclude process mode", async () => {
  const first = validateAndNormalizeReport(
    report({
      processMode: "CLIENT",
      error: "Failure   at C:\\first\\install\\broken.nut",
      frames: [
        {
          function: "Think",
          source: "C:\\first\\scripts\\vscripts\\broken.nut",
          line: 99,
        },
      ],
    }),
  );
  const second = validateAndNormalizeReport(
    report({
      processMode: "DEDICATED",
      error: "Failure at /opt/second/install/broken.nut",
      frames: [
        {
          function: "Think",
          source: "/opt/second/scripts/vscripts/broken.nut",
          line: 99,
        },
      ],
    }),
  );

  const nativeRelative = validateAndNormalizeReport(
    report({
      processMode: "DEDICATED",
      error: "Failure at <path>",
      frames: [
        {
          function: "Think",
          source: "broken.nut",
          line: 99,
        },
      ],
    }),
  );

  const firstFingerprint = await fingerprintReport(first);
  const secondFingerprint = await fingerprintReport(second);
  assert.equal(firstFingerprint, secondFingerprint);
  assert.equal(firstFingerprint, await fingerprintReport(nativeRelative));
  assert.equal(
    firstFingerprint,
    await fingerprintReport({ ...nativeRelative, cascade: true }),
  );

  const changedContext = validateAndNormalizeReport({
    ...first,
    vmContext: "CLIENT",
  });
  assert.notEqual(firstFingerprint, await fingerprintReport(changedContext));

  const changedFrame = validateAndNormalizeReport({
    ...first,
    frames: [{ ...first.frames[0], line: 100 }],
  });
  assert.notEqual(firstFingerprint, await fingerprintReport(changedFrame));
});

test("public codes deterministically extend the SHA-256 prefix", async () => {
  const normalized = validateAndNormalizeReport(report());
  const candidates = publicCodeCandidates(await fingerprintReport(normalized));

  assert.equal(candidates[0].length, 8);
  assert.equal(candidates.at(-1).length, 52);
  assert.equal(new Set(candidates).size, candidates.length);
  assert.ok(candidates.every((code) => /^[0-9ABCDEFGHJKMNPQRSTVWXYZ]+$/.test(code)));
  for (let index = 1; index < candidates.length; index += 1) {
    assert.ok(candidates[index].startsWith(candidates[index - 1]));
    assert.equal(candidates[index].length, candidates[index - 1].length + 2);
  }
});
