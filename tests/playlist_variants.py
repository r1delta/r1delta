"""Make enlarged but valid playlists.txt variants.

usage: playlist_variants.py <in> <out> <target_bytes>

Top-level playlist blocks (depth 1 under "playlists") are cloned under new names
("<name>_pad<N>") until the file reaches target_bytes. Line endings of the input are kept.
"""
import sys

src, dst, target = sys.argv[1], sys.argv[2], int(sys.argv[3])
raw = open(src, 'rb').read().decode('latin1')
nl = '\r\n' if '\r\n' in raw else '\n'
lines = raw.split(nl)
if lines and lines[-1] == '':
    lines.pop()

# locate the closing brace of "playlists" (last line that is just "}")
end = max(i for i, l in enumerate(lines) if l.strip() == '}')
# top-level blocks: header line at depth 1 followed by "{" ... matching "}"
blocks = []
depth = 0
i = 0
cur = None
for i, l in enumerate(lines[:end]):
    s = l.split('//')[0].strip()
    if s == '{':
        depth += 1
        continue
    if s == '}':
        depth -= 1
        if depth == 1 and cur is not None:
            blocks.append((cur, i))
            cur = None
        continue
    if depth == 1 and s.startswith('"') and cur is None and s.count('"') == 2:
        # block header only if the next significant line is "{"
        nxt = lines[i + 1].strip() if i + 1 < len(lines) else ''
        if nxt == '{':
            cur = i
body_blocks = [lines[a:b + 1] for a, b in blocks]
assert body_blocks, 'no blocks found'

out = lines[:end]
size = len(nl.join(out + lines[end:]).encode('latin1')) + len(nl)
n = 0
while size < target:
    n += 1
    for b in body_blocks:
        hdr = b[0].strip().strip('"')
        clone = ['"%s_pad%d"' % (hdr, n)] + b[1:]
        out.extend(clone)
        size += sum(len(x) + len(nl) for x in clone)
        if size >= target:
            break
out.extend(lines[end:])
data = nl.join(out) + nl
open(dst, 'wb').write(data.encode('latin1'))
print(dst, len(data), 'bytes,', len(out), 'lines,', n, 'clone rounds')
