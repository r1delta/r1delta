import assert from "node:assert/strict";
import { test } from "node:test";

import {
  MAX_FRAMES,
  ValidationError,
  fingerprintReport,
  isClassified,
  isModdedGame,
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
    () => validateAndNormalizeReport(report({ schema: 3 })),
    ValidationError,
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ schema: 2 })),
    ValidationError,
    "schema 2 requires the modded member",
  );
  assert.throws(
    () => validateAndNormalizeReport(report({ modded: [] })),
    ValidationError,
    "schema 1 accepts exactly the original keys",
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

function report2(overrides = {}) {
  return {
    ...report({ schema: 2, frames: [{ function: "RunMatch", source: "scripts/vscripts/match.nut", line: 42, modded: false }] }),
    modded: [],
    ...overrides,
  };
}

test("schema 1 fingerprints and public codes are frozen", async () => {
  // Issued by the service before schema 2 existed: these must never change.
  const normalized = validateAndNormalizeReport({
    schema: 1,
    vmContext: "CLIENT",
    processMode: "CLIENT",
    cascade: false,
    error: "the index 'x' does not exist",
    frames: [
      { function: "ClientHudInit", source: "scripts/vscripts/client/cl_main_hud.nut", line: 4276 },
      { function: "main", source: "C:\\g\\scripts\\vscripts\\_init.nut", line: 12 },
    ],
  });
  const fingerprint = await fingerprintReport(normalized);
  assert.equal(
    fingerprint,
    "63dda92179b7b44e083c9554d4192769994fe4407684dfce3eec3e5ad9d0847b",
  );
  assert.equal(publicCodeCandidates(fingerprint)[0], "CFETJ8BS");
  assert.deepEqual(Object.keys(normalized), [
    "schema", "vmContext", "processMode", "cascade", "error", "frames",
  ]);
});

test("schema 2 validates the modded classification", () => {
  const normalized = validateAndNormalizeReport(
    report2({
      modded: ["native", "addons"],
      frames: [
        { function: "A", source: "scripts/vscripts/a.nut", line: 1, modded: false },
        { function: "B", source: "scripts/vscripts/r1bots/_r1bots.nut", line: 2, modded: true },
      ],
    }),
  );
  assert.equal(normalized.schema, 2);
  assert.deepEqual(normalized.moddedReasons, ["addons", "native"], "reasons are listed in canonical order");
  assert.deepEqual(normalized.frames, [
    { function: "A", source: "a.nut", line: 1 },
    { function: "B", source: "r1bots/_r1bots.nut", line: 2, modded: true },
  ]);
  assert.equal(isClassified(normalized), true);
  assert.equal(isModdedGame(normalized), true);

  const stock = validateAndNormalizeReport(report2());
  assert.equal(isClassified(stock), true);
  assert.equal(isModdedGame(stock), false);
  assert.equal(isClassified(validateAndNormalizeReport(report())), false);

  const reject = (overrides) =>
    assert.throws(() => validateAndNormalizeReport(report2(overrides)), ValidationError);
  reject({ modded: "addons" });
  reject({ modded: ["addons", "addons"] });
  reject({ modded: ["mods"] });
  reject({ modded: ["addons", "core", "native", "addons"] });
  reject({ modded: undefined });
  reject({ frames: [{ function: "f", source: "x.nut", line: 1 }] });
  reject({ frames: [{ function: "f", source: "x.nut", line: 1, modded: "yes" }] });
  reject({
    frames: [{ function: "f", source: "x.nut", line: 1, modded: true }],
    modded: [],
  });
  assert.throws(
    () => validateAndNormalizeReport({ ...report(), frames: [{ function: "f", source: "x.nut", line: 1, modded: false }] }),
    ValidationError,
    "schema 1 frames keep exactly the original keys",
  );
});

test("schema 2 fingerprints share codes with schema 1 unless a frame is modded", async () => {
  const legacy = await fingerprintReport(validateAndNormalizeReport(report()));
  const sameError = await fingerprintReport(validateAndNormalizeReport(report2()));
  const moddedGameOnly = await fingerprintReport(
    validateAndNormalizeReport(report2({ modded: ["addons", "core"] })),
  );
  assert.equal(sameError, legacy, "an unmodded schema 2 report is the same error as before");
  assert.equal(moddedGameOnly, legacy, "the game-level flag is counted, not part of the identity");

  const moddedFrame = validateAndNormalizeReport(
    report2({
      modded: ["addons"],
      frames: [{ function: "RunMatch", source: "scripts/vscripts/match.nut", line: 42, modded: true }],
    }),
  );
  const moddedFingerprint = await fingerprintReport(moddedFrame);
  assert.notEqual(moddedFingerprint, legacy, "a modded copy of a stock file is a different error");
  assert.notEqual(
    publicCodeCandidates(moddedFingerprint)[0],
    publicCodeCandidates(legacy)[0],
  );
});
