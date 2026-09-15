from pathlib import Path

path = Path(__file__).resolve().parents[1] / "src/model_renderer.cpp"
text = path.read_text(encoding="utf-8")

blocks = [
    "void preflight_prepared_draw_entry(\n"
    "    const Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry) {\n"
    "    preflight_prepared_draw_entry(framebuffer, entry, {});\n"
    "}\n\n",
    "void execute_prepared_draw_entry(\n"
    "    Framebuffer& framebuffer,\n"
    "    const PreparedDrawOrderEntry& entry,\n"
    "    const Mat4& view,\n"
    "    const Mat4& projection) {\n"
    "    execute_prepared_draw_entry(framebuffer, entry, view, projection, {});\n"
    "}\n\n",
]

for block in blocks:
    count = text.count(block)
    if count != 1:
        raise RuntimeError(f"expected one cleanup block, found {count}")
    text = text.replace(block, "", 1)

path.write_text(text, encoding="utf-8")
print("M87 warning cleanup applied")
