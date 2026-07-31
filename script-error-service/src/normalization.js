export const REPORT_SCHEMA = 1;
export const MAX_REQUEST_BYTES = 16 * 1024;
export const MAX_FRAMES = 32;

export const VM_CONTEXTS = Object.freeze(["SERVER", "CLIENT", "UI"]);
export const PROCESS_MODES = Object.freeze([
  "CLIENT",
  "LISTEN",
  "DEDICATED",
]);

const VM_CONTEXT_SET = new Set(VM_CONTEXTS);
const PROCESS_MODE_SET = new Set(PROCESS_MODES);
const REPORT_KEYS = Object.freeze([
  "schema",
  "vmContext",
  "processMode",
  "cascade",
  "error",
  "frames",
]);
const FRAME_KEYS = Object.freeze(["function", "source", "line"]);
const CROCKFORD_BASE32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const PUBLIC_CODE_LENGTHS = Object.freeze(
  Array.from({ length: 23 }, (_, index) => 8 + index * 2),
);

export class ValidationError extends Error {
  constructor(message) {
    super(message);
    this.name = "ValidationError";
  }
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function assertExactKeys(value, expected, label) {
  if (!isRecord(value)) {
    throw new ValidationError(`${label} must be an object`);
  }

  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (
    actual.length !== wanted.length ||
    actual.some((key, index) => key !== wanted[index])
  ) {
    throw new ValidationError(`${label} has unexpected or missing fields`);
  }
}

function assertString(value, label, maximumLength) {
  if (typeof value !== "string") {
    throw new ValidationError(`${label} must be a string`);
  }
  if (value.length > maximumLength) {
    throw new ValidationError(`${label} is too long`);
  }
}

function normalizeUnicodeAndControls(value) {
  return value
    .normalize("NFC")
    .replace(/\r\n?/g, "\n")
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, " ")
    .replace(/[\u200B-\u200F\u202A-\u202E\u2060\u2066-\u2069\uFEFF]/g, "");
}

function replaceIpv4(text) {
  return text.replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, (candidate) => {
    const octets = candidate.split(".").map(Number);
    return octets.every((octet) => octet >= 0 && octet <= 255)
      ? "<ip>"
      : candidate;
  });
}

/**
 * Removes data that can identify a person, device, account, or installation.
 * This is defense in depth: callers must still send only the report schema.
 */
export function redactSensitiveData(value) {
  let text = normalizeUnicodeAndControls(value);

  text = text.replace(
    /(^|\s)(?:locals?|local variables?|diagprints?|diagnostic prints?|registers?|stack slots?)\s*[:=][^\r\n]*/giu,
    (_, prefix) => `${prefix}<details redacted>`,
  );
  text = text.replace(
    /(^|[^\p{L}\p{N}_])"[^"\r\n]*"(?=$|[^\p{L}\p{N}_])/gu,
    (_, prefix) => `${prefix}"<redacted>"`,
  );
  text = text.replace(
    /(^|[^\p{L}\p{N}_])'[^'\r\n]*'(?=$|[^\p{L}\p{N}_])/gu,
    (_, prefix) => `${prefix}'<redacted>'`,
  );

  text = text.replace(
    /"(?:file:\/{2,3}|[A-Za-z]:[\\/]|\\\\|\/)[^"\r\n]+"/giu,
    "<path>",
  );
  text = text.replace(
    /'(?:file:\/{2,3}|[A-Za-z]:[\\/]|\\\\|\/)[^'\r\n]+'/giu,
    "<path>",
  );
  text = text.replace(/\b(?:https?|ftp):\/\/[^\s<>"']+/giu, "<url>");
  text = text.replace(/\bfile:\/{2,3}[^\s<>"']+/giu, "<path>");
  text = text.replace(
    /\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b/giu,
    "<email>",
  );
  text = text.replace(
    /\b[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\b/giu,
    "<id>",
  );
  text = text.replace(/\bSTEAM_[0-5]:[01]:\d+\b/giu, "<id>");
  text = text.replace(/\b7656119\d{10}\b/g, "<id>");
  text = text.replace(/\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b/giu, "<device>");
  text = text.replace(
    /(^|[^\w:])(?:[0-9a-f]{1,4}:){0,7}::(?:[0-9a-f]{1,4}:){0,7}[0-9a-f]{0,4}(?=$|[^\w:])/giu,
    (_, prefix) => `${prefix}<ip>`,
  );
  text = text.replace(
    /\b(?:[0-9a-f]{1,4}:){2,7}[0-9a-f]{1,4}\b/giu,
    "<ip>",
  );
  text = replaceIpv4(text);
  text = text.replace(
    /\b(?:account|player|device)(?:[\s_-]*(?:id|uid|guid))?\s*[:=]\s*[^\s,;]+/giu,
    "<id>",
  );
  text = text.replace(/\b0x[0-9a-f]{8,16}\b/giu, "<address>");
  text = text.replace(/\b\d{16,20}\b/g, "<id>");
  text = text.replace(
    /(?:[A-Za-z]:[\\/]|\\\\)[^\r\n<>"',;]+/g,
    "<path>",
  );
  text = text.replace(
    /(^|[\s("'=])\/[^\r\n<>"',;]+/gm,
    (_, prefix) => `${prefix}<path>`,
  );

  return text;
}

export function normalizeError(value) {
  return redactSensitiveData(value).replace(/\s+/gu, " ").trim();
}

export function normalizeFunction(value) {
  const normalized = redactSensitiveData(value).replace(/\s+/gu, " ").trim();
  return normalized || "<anonymous>";
}

export function normalizeSource(value) {
  let source = normalizeUnicodeAndControls(value).trim().replace(/\\/g, "/");
  source = source.replace(/^file:\/{2,3}/i, "/");

  const isAbsolute =
    /^[A-Za-z]:\//.test(source) || source.startsWith("//") || source.startsWith("/");
  const lowerSource = source.toLowerCase();
  const markers = ["scripts/vscripts/", "vscripts/"];
  const marker = markers
    .map((candidate) => ({ candidate, index: lowerSource.indexOf(candidate) }))
    .filter(({ index }) => index >= 0)
    .sort((left, right) => left.index - right.index)[0];

  if (marker) {
    source = source.slice(marker.index + marker.candidate.length);
  } else if (isAbsolute) {
    const basename = source.split("/").filter(Boolean).at(-1);
    source = basename ? `<path>/${basename}` : "<path>";
  }

  source = source
    .split("/")
    .filter((segment) => segment && segment !== "." && segment !== "..")
    .join("/");
  source = redactSensitiveData(source).replace(/\s+/gu, " ").trim().toLowerCase();
  return source || "<unknown>";
}

export function validateAndNormalizeReport(value) {
  assertExactKeys(value, REPORT_KEYS, "report");

  if (value.schema !== REPORT_SCHEMA) {
    throw new ValidationError(`schema must be ${REPORT_SCHEMA}`);
  }
  if (!VM_CONTEXT_SET.has(value.vmContext)) {
    throw new ValidationError("vmContext is invalid");
  }
  if (!PROCESS_MODE_SET.has(value.processMode)) {
    throw new ValidationError("processMode is invalid");
  }
  if (typeof value.cascade !== "boolean") {
    throw new ValidationError("cascade must be a boolean");
  }

  assertString(value.error, "error", 8192);
  if (!Array.isArray(value.frames)) {
    throw new ValidationError("frames must be an array");
  }
  if (value.frames.length > MAX_FRAMES) {
    throw new ValidationError(`frames may contain at most ${MAX_FRAMES} entries`);
  }

  const error = normalizeError(value.error);
  if (!error) {
    throw new ValidationError("error must not be empty");
  }

  const frames = value.frames.map((frame, index) => {
    const label = `frames[${index}]`;
    assertExactKeys(frame, FRAME_KEYS, label);
    assertString(frame.function, `${label}.function`, 512);
    assertString(frame.source, `${label}.source`, 1024);
    if (
      !Number.isSafeInteger(frame.line) ||
      frame.line < 0 ||
      frame.line > 2_147_483_647
    ) {
      throw new ValidationError(`${label}.line is invalid`);
    }

    return {
      function: normalizeFunction(frame.function),
      source: normalizeSource(frame.source),
      line: frame.line,
    };
  });

  return {
    schema: REPORT_SCHEMA,
    vmContext: value.vmContext,
    processMode: value.processMode,
    cascade: value.cascade,
    error,
    frames,
  };
}

export function canonicalFingerprintInput(report) {
  return JSON.stringify([
    REPORT_SCHEMA,
    report.vmContext,
    report.error,
    report.frames.map((frame) => [frame.function, frame.source, frame.line]),
  ]);
}

function bytesToHex(bytes) {
  let result = "";
  for (const byte of bytes) {
    result += byte.toString(16).padStart(2, "0");
  }
  return result;
}

async function sha256Bytes(value) {
  const encoded = new TextEncoder().encode(value);
  return new Uint8Array(await globalThis.crypto.subtle.digest("SHA-256", encoded));
}

export async function fingerprintReport(report) {
  return bytesToHex(await sha256Bytes(canonicalFingerprintInput(report)));
}

function fingerprintHexToBase32(fingerprint) {
  if (!/^[0-9a-f]{64}$/i.test(fingerprint)) {
    throw new TypeError("fingerprint must be a SHA-256 hex digest");
  }

  const bytes = new Uint8Array(32);
  for (let index = 0; index < bytes.length; index += 1) {
    bytes[index] = Number.parseInt(fingerprint.slice(index * 2, index * 2 + 2), 16);
  }

  let output = "";
  let accumulator = 0;
  let availableBits = 0;
  for (const byte of bytes) {
    accumulator = (accumulator << 8) | byte;
    availableBits += 8;
    while (availableBits >= 5) {
      availableBits -= 5;
      output += CROCKFORD_BASE32[(accumulator >>> availableBits) & 31];
      accumulator &= (1 << availableBits) - 1;
    }
  }
  if (availableBits > 0) {
    output += CROCKFORD_BASE32[(accumulator << (5 - availableBits)) & 31];
  }
  return output;
}

export function publicCodeCandidates(fingerprint) {
  const encoded = fingerprintHexToBase32(fingerprint);
  return PUBLIC_CODE_LENGTHS.map((length) => encoded.slice(0, length));
}
