"""Run the addon in an isolated Lua 5.1 runtime with UI stubs."""
import argparse
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--lupa-path", help="Optional directory containing the lupa package")
args = parser.parse_args()
if args.lupa_path:
    sys.path.insert(0, args.lupa_path)
from lupa.lua51 import LuaRuntime

TESTS = Path(__file__).resolve().parent
ADDON = TESTS.parents[1] / "addons/WoWCompagnon"
for locale in ("frFR", "enUS", "deDE"):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().TestLocale = locale
    lua.execute((TESTS / "addon_ui_stubs.lua").read_text(encoding="utf-8"))
    for name in ("Data.lua", "Model.lua", "Form.lua"):
        text = (ADDON / name).read_text(encoding="utf-8")
        lua.execute("assert(loadstring(...))", text)
        lua.execute(text)
    lua.execute((TESTS / "addon_form_test.lua").read_text(encoding="utf-8"))
    print(locale + ": Lua 5.1 PASS")
