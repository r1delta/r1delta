const encoder = new TextEncoder();
const MAX_TOKEN_LENGTH = 1024;

async function digestToken(token) {
  return new Uint8Array(
    await globalThis.crypto.subtle.digest("SHA-256", encoder.encode(token)),
  );
}

export async function constantTimeTokenEqual(candidate, expected) {
  if (typeof candidate !== "string" || typeof expected !== "string") {
    return false;
  }
  if (
    candidate.length === 0 ||
    expected.length === 0 ||
    candidate.length > MAX_TOKEN_LENGTH ||
    expected.length > MAX_TOKEN_LENGTH
  ) {
    return false;
  }

  const [candidateDigest, expectedDigest] = await Promise.all([
    digestToken(candidate),
    digestToken(expected),
  ]);
  let difference = 0;
  for (let index = 0; index < expectedDigest.length; index += 1) {
    difference |= candidateDigest[index] ^ expectedDigest[index];
  }
  return difference === 0;
}

export async function isAuthorized(request, expectedToken) {
  if (typeof expectedToken !== "string" || expectedToken.length === 0) {
    return false;
  }

  const authorization = request.headers.get("authorization");
  const match = authorization?.match(/^Bearer ([^\s]+)$/i);
  if (!match) {
    return false;
  }

  return constantTimeTokenEqual(match[1], expectedToken);
}

export function withSecurityHeaders(response, { html = false } = {}) {
  const headers = new Headers(response.headers);
  headers.set("Cache-Control", "no-store");
  headers.set(
    "Content-Security-Policy",
    html
      ? "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"
      : "default-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
  );
  headers.set("Cross-Origin-Opener-Policy", "same-origin");
  headers.set("Cross-Origin-Resource-Policy", "same-origin");
  headers.set("Permissions-Policy", "camera=(), geolocation=(), microphone=(), payment=(), usb=()");
  headers.set("Referrer-Policy", "no-referrer");
  headers.set("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  headers.set("X-Content-Type-Options", "nosniff");
  headers.set("X-Frame-Options", "DENY");
  headers.set("X-Robots-Tag", "noindex, nofollow");
  headers.set("X-XSS-Protection", "0");

  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
}
