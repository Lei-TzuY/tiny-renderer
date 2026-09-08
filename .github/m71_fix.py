from pathlib import Path

mtl = Path("tests/fixtures/shininess_textured.mtl")
text = mtl.read_text()
old = "map_Ks shininess_map.ppm\nmap_Ns shininess_map.ppm\n"
new = "map_Ks checker.ppm\nmap_Ns checker.ppm\n"
if text.count(old) != 1:
    raise RuntimeError("unexpected shininess MTL fixture state")
mtl.write_text(text.replace(old, new, 1))

test = Path("tests/test_shininess_texture.cpp")
text = test.read_text()
old = "    check(inspect_model_asset(imported).find(\"shininess_texture=1x1\") != std::string::npos,\n          \"asset inspection exposes map_Ns dimensions\");\n"
new = "    const std::string inspection = inspect_model_asset(imported);\n    check(inspection.find(\"shininess_texture=\") != std::string::npos\n              && inspection.find(\"shininess_texture=none\") == std::string::npos,\n          \"asset inspection exposes owned map_Ns dimensions\");\n"
if text.count(old) != 1:
    raise RuntimeError("unexpected shininess inspection assertion state")
test.write_text(text.replace(old, new, 1))
