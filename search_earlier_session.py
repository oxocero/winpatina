import json
import sys

filepath = "C:/Users/oxocero/.claude/projects/C--Users-oxocero-Desktop-repos-winpatina/18fc68c8-ce28-4841-97e2-0e4ee0c0ae2c.jsonl"

with open(filepath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

print(f"Total lines in earlier session: {len(lines)}")

# Search for demo-related terms in user messages
keywords = [
    'tracker', 'music', 'demo', 'game', 'screensaver', 'idea', 'suggest',
    'paint', 'bejewel', 'breakout', 'flappy', 'water', 'doodle',
    'clone', 'app', 'player', 'audio', 'mod player', 'chiptune',
    'launcher', 'entry', 'entries', 'add', 'propose', 'how about',
    'we should', 'could add', 'could make'
]

results = []
for i, line in enumerate(lines):
    try:
        obj = json.loads(line.strip())
        if obj.get('type') != 'user':
            continue
        msg = obj.get('message', {})
        content = msg.get('content', '')

        if isinstance(content, str):
            text = content
        elif isinstance(content, list):
            texts = []
            for item in content:
                if isinstance(item, dict):
                    if item.get('type') == 'text':
                        texts.append(item.get('text', ''))
            text = ' '.join(texts)
        else:
            continue

        text_lower = text.lower()
        matched = [kw for kw in keywords if kw in text_lower]
        if matched and len(text.strip()) > 5 and len(text.strip()) < 5000:
            results.append((i+1, matched, text.strip()[:500]))
    except:
        pass

outpath = "C:/Users/oxocero/Desktop/repos/winpatina/search_earlier_results.txt"
with open(outpath, 'w', encoding='utf-8') as out:
    out.write(f"Found {len(results)} matching user messages in earlier session\n")
    out.write("=" * 80 + "\n")
    for line_no, matched, preview in results:
        out.write(f"\nLINE {line_no} | Keywords: {matched}\n")
        out.write(f"  TEXT: {preview}\n")
        out.write("---\n")

print(f"Found {len(results)} results")
