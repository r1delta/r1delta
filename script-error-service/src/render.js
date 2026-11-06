export function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function page(title, body) {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>${escapeHtml(title)}</title>
<style>
:root{color-scheme:dark;font:16px/1.5 system-ui,sans-serif;background:#10141a;color:#e8edf2}
body{max-width:72rem;margin:0 auto;padding:2rem}a{color:#8ecbff}code,pre{font-family:ui-monospace,monospace}
.card{background:#171e27;border:1px solid #2e3947;border-radius:.5rem;padding:1rem;margin:1rem 0}
pre{white-space:pre-wrap;overflow-wrap:anywhere}.muted{color:#9cabbc}table{width:100%;border-collapse:collapse}
th,td{text-align:left;vertical-align:top;padding:.6rem;border-bottom:1px solid #2e3947}th{color:#b9c8d8}
.modded{color:#ffb86b;font-weight:600}
</style>
</head>
<body>${body}</body>
</html>`;
}

function renderFrame(frame) {
  const location = `at ${escapeHtml(frame.function)} (${escapeHtml(frame.source)}:${escapeHtml(frame.line)})`;
  return frame.modded
    ? `${location} <span class="modded">[modded file]</span>`
    : location;
}

function percent(part, whole) {
  return `${Math.round((part / whole) * 100)}%`;
}

// Reports from clients that predate schema 2 are unclassified, so the share is of classified
// occurrences only.
function describePublicModdedShare(error) {
  if (!error.classifiedCount) {
    return "modded status unknown";
  }
  return `${error.moddedCount} of ${error.classifiedCount} classified occurrences from modded games (${percent(error.moddedCount, error.classifiedCount)})`;
}

function describeAdminModdedShare(row) {
  if (!row.windowClassifiedCount) {
    return "unknown";
  }
  return `${row.windowModdedCount} / ${row.windowClassifiedCount} (${percent(row.windowModdedCount, row.windowClassifiedCount)})`;
}

const MODDED_FILTER_LABELS = Object.freeze({
  all: "all occurrences",
  exclude: "modded games excluded",
  only: "modded games only",
});

export function renderPublicError(error) {
  const frames = error.frames.length
    ? error.frames.map(renderFrame).join("\n")
    : "No frames were reported.";

  const processTypes = error.processTypes?.length
    ? error.processTypes
        .map((entry) => `${entry.processType} (${entry.count})`)
        .join(", ")
    : "No process-type data";

  return page(
    `Script error #${error.code}`,
    `<main>
<h1>Script error <code>#${escapeHtml(error.code)}</code></h1>
<p class="muted">${escapeHtml(error.vmContext)} VM · ${escapeHtml(processTypes)} · ${escapeHtml(error.totalCount)} occurrence${error.totalCount === 1 ? "" : "s"} · ${escapeHtml(error.cascadeCount)} within 30 seconds of another script error · ${escapeHtml(describePublicModdedShare(error))}</p>
<section class="card" aria-labelledby="error-heading">
<h2 id="error-heading">Error</h2>
<pre>${escapeHtml(error.error)}</pre>
</section>
<section class="card" aria-labelledby="frames-heading">
<h2 id="frames-heading">Frames</h2>
<pre>${frames}</pre>
</section>
<dl>
<dt>First seen</dt><dd>${escapeHtml(error.firstSeen)}</dd>
<dt>Last seen</dt><dd>${escapeHtml(error.lastSeen)}</dd>
</dl>
</main>`,
  );
}

function abbreviatedError(error) {
  return error.length <= 240 ? error : `${error.slice(0, 239)}…`;
}

export function renderAdminPage(
  rows,
  { windowName, generatedAt, moddedFilter = "all" },
) {
  const tableRows = rows.length
    ? rows
        .map(
          (row) => `<tr>
<td><a href="/errors/${encodeURIComponent(row.code)}"><code>#${escapeHtml(row.code)}</code></a></td>
<td>${escapeHtml(row.vmContext)}</td>
<td>${escapeHtml(row.processMode)}</td>
<td>${escapeHtml(row.windowCount)}</td>
<td>${escapeHtml(row.totalCount)}</td>
<td>${escapeHtml(row.windowCascadeCount)} / ${escapeHtml(row.cascadeCount)}</td>
<td>${escapeHtml(describeAdminModdedShare(row))}</td>
<td>${escapeHtml(abbreviatedError(row.error))}</td>
<td>${escapeHtml(row.lastSeen)}</td>
</tr>`,
        )
        .join("")
    : '<tr><td colspan="9">No errors in this window.</td></tr>';

  return page(
    "R1Delta script errors",
    `<main>
<h1>R1Delta script errors</h1>
<p class="muted">Top context/mode buckets for ${escapeHtml(windowName)} · ${escapeHtml(MODDED_FILTER_LABELS[moddedFilter] ?? moddedFilter)} · generated ${escapeHtml(generatedAt)}. Cascade counts are not filtered by modded status.</p>
<div class="card">
<table>
<thead><tr><th>Code</th><th>VM</th><th>Process type</th><th>Window</th><th>All time</th><th>Cascades (window / all)</th><th>Modded (window)</th><th>Error</th><th>Last seen</th></tr></thead>
<tbody>${tableRows}</tbody>
</table>
</div>
</main>`,
  );
}
