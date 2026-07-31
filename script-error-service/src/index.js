import {
  MAX_REQUEST_BYTES,
  ValidationError,
  fingerprintReport,
  publicCodeCandidates,
  validateAndNormalizeReport,
} from "./normalization.js";
import { renderAdminPage, renderPublicError } from "./render.js";
import { isAuthorized, withSecurityHeaders } from "./security.js";
import {
  checkDatabase,
  findPublicError,
  findTopErrors,
  recordOccurrence,
} from "./storage.js";

const PUBLIC_CODE_PATTERN = /^[0-9ABCDEFGHJKMNPQRSTVWXYZ]{8,52}$/;
const ADMIN_WINDOWS = Object.freeze({
  "24h": 24 * 60 * 60,
  "7d": 7 * 24 * 60 * 60,
  "30d": 30 * 24 * 60 * 60,
  all: null,
});

class HttpError extends Error {
  constructor(status, code, message, headers = undefined) {
    super(message);
    this.name = "HttpError";
    this.status = status;
    this.code = code;
    this.headers = headers;
  }
}

function jsonResponse(value, status = 200, headers = undefined) {
  const responseHeaders = new Headers(headers);
  responseHeaders.set("Content-Type", "application/json; charset=utf-8");
  return withSecurityHeaders(
    new Response(JSON.stringify(value), { status, headers: responseHeaders }),
  );
}

function htmlResponse(value, status = 200, headers = undefined) {
  const responseHeaders = new Headers(headers);
  responseHeaders.set("Content-Type", "text/html; charset=utf-8");
  return withSecurityHeaders(
    new Response(value, { status, headers: responseHeaders }),
    { html: true },
  );
}

function methodNotAllowed(allowed) {
  throw new HttpError(405, "method_not_allowed", "Method not allowed", {
    Allow: allowed,
  });
}

export function isJsonContentType(value) {
  if (typeof value !== "string") {
    return false;
  }

  const parts = value.split(";").map((part) => part.trim());
  if (parts.shift()?.toLowerCase() !== "application/json") {
    return false;
  }
  return (
    parts.length === 0 ||
    (parts.length === 1 && /^charset=(?:utf-8|"utf-8")$/i.test(parts[0]))
  );
}

function checkDeclaredBodyLength(request) {
  const rawLength = request.headers.get("content-length");
  if (rawLength === null) {
    return;
  }
  if (!/^\d+$/.test(rawLength)) {
    throw new HttpError(400, "invalid_content_length", "Invalid Content-Length");
  }
  if (Number(rawLength) > MAX_REQUEST_BYTES) {
    throw new HttpError(413, "body_too_large", "Request body exceeds 16 KiB");
  }
}

async function readJsonBody(request) {
  checkDeclaredBodyLength(request);

  if (!request.body) {
    throw new HttpError(400, "invalid_json", "Request body must contain JSON");
  }

  const reader = request.body.getReader();
  const chunks = [];
  let totalLength = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) {
        break;
      }
      totalLength += value.byteLength;
      if (totalLength > MAX_REQUEST_BYTES) {
        try {
          await reader.cancel();
        } catch {
          // The size failure remains authoritative if stream cancellation fails.
        }
        throw new HttpError(413, "body_too_large", "Request body exceeds 16 KiB");
      }
      chunks.push(value);
    }
  } finally {
    reader.releaseLock();
  }

  const bytes = new Uint8Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }

  let text;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    throw new HttpError(400, "invalid_json", "Request body must be UTF-8 JSON");
  }

  try {
    return JSON.parse(text);
  } catch {
    throw new HttpError(400, "invalid_json", "Request body contains invalid JSON");
  }
}

async function enforceIpRateLimit(request, env, scope) {
  if (!env.REPORT_RATE_LIMIT?.limit) {
    throw new HttpError(503, "reporting_unavailable", "Reporting is unavailable");
  }

  const connectingIp = request.headers.get("cf-connecting-ip") || "missing";
  let result;
  try {
    result = await env.REPORT_RATE_LIMIT.limit({ key: `${scope}:${connectingIp}` });
  } catch {
    throw new HttpError(503, "reporting_unavailable", "Reporting is unavailable");
  }
  if (!result.success) {
    throw new HttpError(429, "rate_limited", "Too many requests", {
      "Retry-After": "60",
    });
  }
}

async function postReport(request, env) {
  if (!isJsonContentType(request.headers.get("content-type"))) {
    throw new HttpError(
      415,
      "unsupported_media_type",
      "Content-Type must be application/json",
    );
  }
  const contentEncoding = request.headers.get("content-encoding");
  if (contentEncoding && contentEncoding.toLowerCase() !== "identity") {
    throw new HttpError(
      415,
      "unsupported_content_encoding",
      "Compressed request bodies are not supported",
    );
  }

  checkDeclaredBodyLength(request);
  const input = await readJsonBody(request);

  let report;
  try {
    report = validateAndNormalizeReport(input);
  } catch (error) {
    if (error instanceof ValidationError) {
      throw new HttpError(400, "invalid_report", error.message);
    }
    throw error;
  }

  await enforceIpRateLimit(request, env, "report");

  const fingerprint = await fingerprintReport(report);
  const result = await recordOccurrence(
    env.DB,
    report,
    fingerprint,
    publicCodeCandidates(fingerprint),
  );
  return jsonResponse(result);
}

function parsePublicCode(pathname, prefix) {
  if (!pathname.startsWith(prefix)) {
    return null;
  }
  const rawCode = pathname.slice(prefix.length);
  if (!rawCode || rawCode.includes("/")) {
    return null;
  }
  const code = rawCode.toUpperCase();
  return PUBLIC_CODE_PATTERN.test(code) ? code : null;
}

async function requireAdmin(request, env) {
  if (typeof env.ADMIN_TOKEN !== "string" || env.ADMIN_TOKEN.length === 0) {
    throw new HttpError(
      503,
      "admin_auth_unavailable",
      "Admin authentication is unavailable",
    );
  }
  if (!(await isAuthorized(request, env.ADMIN_TOKEN))) {
    throw new HttpError(401, "unauthorized", "A valid bearer token is required", {
      "WWW-Authenticate": 'Bearer realm="r1delta-script-errors"',
    });
  }
}

function parseAdminQuery(url, now = new Date()) {
  const allowedParameters = new Set(["window", "limit"]);
  for (const key of url.searchParams.keys()) {
    if (!allowedParameters.has(key) || url.searchParams.getAll(key).length !== 1) {
      throw new HttpError(400, "invalid_query", "Invalid admin query");
    }
  }

  const windowName = url.searchParams.get("window") || "7d";
  if (!Object.hasOwn(ADMIN_WINDOWS, windowName)) {
    throw new HttpError(400, "invalid_query", "Invalid ranking window");
  }

  const rawLimit = url.searchParams.get("limit") || "25";
  if (!/^[1-9]\d{0,2}$/.test(rawLimit)) {
    throw new HttpError(400, "invalid_query", "Invalid ranking limit");
  }
  const limit = Number(rawLimit);
  if (limit > 100) {
    throw new HttpError(400, "invalid_query", "Ranking limit may not exceed 100");
  }

  const duration = ADMIN_WINDOWS[windowName];
  const nowSeconds = Math.floor(now.getTime() / 1000);
  const cutoff =
    duration === null ? 0 : Math.floor((nowSeconds - duration) / 3600) * 3600;
  return { windowName, cutoff, limit };
}

async function getAdminTop(request, env, url, html) {
  await requireAdmin(request, env);
  const query = parseAdminQuery(url);
  const items = await findTopErrors(env.DB, query.cutoff, query.limit);
  const generatedAt = new Date().toISOString();

  if (html) {
    return htmlResponse(
      renderAdminPage(items, {
        windowName: query.windowName,
        generatedAt,
      }),
    );
  }
  return jsonResponse({
    window: query.windowName,
    generatedAt,
    items,
  });
}

async function route(request, env) {
  const url = new URL(request.url);
  const { pathname } = url;

  if (pathname === "/health") {
    if (request.method !== "GET") {
      methodNotAllowed("GET");
    }
    if (!(await checkDatabase(env.DB))) {
      throw new HttpError(503, "unhealthy", "Service is unhealthy");
    }
    return jsonResponse({ status: "ok" });
  }

  if (pathname === "/v1/reports") {
    if (request.method !== "POST") {
      methodNotAllowed("POST");
    }
    return postReport(request, env);
  }

  if (pathname === "/v1/admin/top") {
    if (request.method !== "GET") {
      methodNotAllowed("GET");
    }
    return getAdminTop(request, env, url, false);
  }

  if (pathname === "/admin") {
    if (request.method !== "GET") {
      methodNotAllowed("GET");
    }
    return getAdminTop(request, env, url, true);
  }

  if (pathname.startsWith("/v1/errors/")) {
    if (request.method !== "GET") {
      methodNotAllowed("GET");
    }
    const code = parsePublicCode(pathname, "/v1/errors/");
    await enforceIpRateLimit(request, env, "lookup");
    const error = code ? await findPublicError(env.DB, code) : null;
    if (!error) {
      throw new HttpError(404, "not_found", "Script error not found");
    }
    return jsonResponse(error);
  }

  if (pathname.startsWith("/errors/")) {
    if (request.method !== "GET") {
      methodNotAllowed("GET");
    }
    const code = parsePublicCode(pathname, "/errors/");
    await enforceIpRateLimit(request, env, "lookup");
    const error = code ? await findPublicError(env.DB, code) : null;
    if (!error) {
      return htmlResponse(
        "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>Not found</title><h1>Script error not found</h1></html>",
        404,
      );
    }
    return htmlResponse(renderPublicError(error));
  }

  throw new HttpError(404, "not_found", "Route not found");
}

export default {
  async fetch(request, env) {
    try {
      return await route(request, env);
    } catch (error) {
      if (error instanceof HttpError) {
        return jsonResponse(
          { error: { code: error.code, message: error.message } },
          error.status,
          error.headers,
        );
      }
      return jsonResponse(
        { error: { code: "internal_error", message: "Internal server error" } },
        500,
      );
    }
  },
};
