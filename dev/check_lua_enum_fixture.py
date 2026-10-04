"""Keep the bounded registration identical to the original 143-key Lua API."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'mod/uevr/src/mods/bindings/ImGui.cpp').read_text(encoding='utf-8')
fixture = (root / 'dev/key-enum-fixture.inc').read_text(encoding='utf-8')
block = re.search(r'wuwa_lua::read_only_enum\(imgui, "ImGuiKey", \{(.*?)\n    \}\);', source, re.S)
assert block, 'Bounded ImGuiKey registration is missing'
pattern = r'\{"([^"]+)",\s*(\w+)\}'
expected = re.findall(pattern, fixture)
assert len(expected) == 143 and len({name for name, _ in expected}) == 143
assert re.findall(pattern, block.group(1)) == expected, 'Lua key names or enum values changed'
print('PASS all 143 Lua key names and enum values match the original fixture')
