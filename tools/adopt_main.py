from pathlib import Path
r=Path(__file__).resolve().parents[1]
(r/'main.cpp').write_text((r/'src/main.cpp').read_text(encoding='utf-8'),encoding='utf-8')
