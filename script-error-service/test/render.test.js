import assert from "node:assert/strict";
import { test } from "node:test";

import { escapeHtml, renderAdminPage, renderPublicError } from "../src/render.js";

test("HTML escaping covers text and attribute metacharacters", () => {
  assert.equal(
    escapeHtml(`<tag a="x">Tom & 'friend'</tag>`),
    "&lt;tag a=&quot;x&quot;&gt;Tom &amp; &#39;friend&#39;&lt;/tag&gt;",
  );
});

test("public error rendering never interprets report content as markup", () => {
  const html = renderPublicError({
    code: "ABC12345",
    vmContext: "SERVER<script>",
    processTypes: [{ processType: "DEDICATED", count: 2 }],
    cascadeCount: 1,
    error: `<script>alert("x")</script> & 'quoted'`,
    frames: [
      {
        function: `<img src=x onerror="alert(1)">`,
        source: `bad<&>"'.nut`,
        line: 12,
      },
    ],
    firstSeen: "2026-07-26T00:00:00.000Z",
    lastSeen: "2026-07-26T01:00:00.000Z",
    totalCount: 2,
  });

  assert.doesNotMatch(html, /<script>|<img src=/i);
  assert.match(html, /&lt;script&gt;alert\(&quot;x&quot;\)&lt;\/script&gt;/);
  assert.match(html, /&lt;img src=x onerror=&quot;alert\(1\)&quot;&gt;/);
  assert.match(html, /bad&lt;&amp;&gt;&quot;&#39;\.nut/);
  assert.match(html, /&amp; &#39;quoted&#39;/);
  assert.match(html, /DEDICATED \(2\)/);
  assert.match(html, /1 within 30 seconds/);
});

test("admin rendering escapes stored errors and labels", () => {
  const html = renderAdminPage(
    [
      {
        code: "ABC12345",
        vmContext: "UI",
        processMode: "CLIENT",
        windowCount: 3,
        totalCount: 4,
        windowCascadeCount: 2,
        cascadeCount: 3,
        error: "<svg onload=alert(1)>",
        lastSeen: "2026-07-26T01:00:00.000Z",
      },
    ],
    {
      windowName: "<all>",
      generatedAt: "2026-07-26T02:00:00.000Z",
    },
  );

  assert.doesNotMatch(html, /<svg|<all>/i);
  assert.match(html, /&lt;svg onload=alert\(1\)&gt;/);
  assert.match(html, /&lt;all&gt;/);
  assert.match(html, />2 \/ 3</);
});

test("public page marks modded frames and states the modded share of classified reports", () => {
  const html = renderPublicError({
    code: "ABC12345",
    vmContext: "CLIENT",
    processTypes: [],
    cascadeCount: 0,
    classifiedCount: 4,
    moddedCount: 1,
    error: "boom",
    frames: [
      { function: "Stock", source: "client/_cl_material_proxies.nut", line: 721 },
      { function: "FOVReticleOffsetKatie", source: "client/_cl_material_proxies.nut", line: 721, modded: true },
    ],
    firstSeen: "2026-07-26T00:00:00.000Z",
    lastSeen: "2026-07-26T01:00:00.000Z",
    totalCount: 9,
  });

  assert.equal(html.match(/\[modded file\]/g).length, 1);
  assert.match(html, /FOVReticleOffsetKatie[^\n]*\[modded file\]/);
  assert.doesNotMatch(html, /Stock[^\n]*\[modded file\]/);
  assert.match(html, /1 of 4 classified occurrences from modded games \(25%\)/);

  const unclassified = renderPublicError({
    code: "ABC12345",
    vmContext: "CLIENT",
    cascadeCount: 0,
    classifiedCount: 0,
    moddedCount: 0,
    error: "x",
    frames: [],
    firstSeen: "2026-07-26T00:00:00.000Z",
    lastSeen: "2026-07-26T01:00:00.000Z",
    totalCount: 1,
  });
  assert.match(unclassified, /modded status unknown/);
});

test("admin page shows the modded share column and the active filter", () => {
  const row = {
    code: "ABC12345",
    vmContext: "CLIENT",
    processMode: "CLIENT",
    windowCount: 8,
    totalCount: 8,
    windowCascadeCount: 0,
    cascadeCount: 0,
    windowClassifiedCount: 4,
    windowModdedCount: 3,
    error: "boom",
    lastSeen: "2026-07-26T01:00:00.000Z",
  };
  const html = renderAdminPage([row, { ...row, code: "ZZZZZZZZ", windowClassifiedCount: 0, windowModdedCount: 0 }], {
    windowName: "7d",
    moddedFilter: "exclude",
    generatedAt: "2026-07-26T02:00:00.000Z",
  });

  assert.match(html, /<th>Modded \(window\)<\/th>/);
  assert.match(html, />3 \/ 4 \(75%\)</);
  assert.match(html, />unknown</);
  assert.match(html, /modded games excluded/);
});
