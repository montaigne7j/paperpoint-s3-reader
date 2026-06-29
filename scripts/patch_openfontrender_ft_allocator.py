Import("env")

from pathlib import Path


def patch_file(path: Path) -> bool:
    if not path.exists():
        raise RuntimeError(f"OpenFontRender source was not found: {path}")

    text = path.read_text(encoding="utf-8")
    original = text

    if "FreeTypePsramAllocator.h" not in text:
        needle = '#include "OpenFontRender.h"'
        if needle not in text:
            raise RuntimeError("Cannot patch OpenFontRender.cpp: include anchor not found")
        text = text.replace(needle, needle + '\n#include <FreeTypePsramAllocator.h>', 1)

    init_needle = "error = FT_Init_FreeType(&g_FtLibrary);"
    init_repl = "error = CrossPointFtPsramAllocator::newLibrary(&g_FtLibrary);"
    if init_needle in text:
        text = text.replace(init_needle, init_repl, 1)
    elif init_repl not in text:
        raise RuntimeError("Cannot patch OpenFontRender.cpp: FT_Init_FreeType anchor not found")

    text = text.replace("FT_Init_FreeType error", "FT_New_Library(psram) error")

    done_needle = "FT_Done_FreeType(g_FtLibrary);"
    done_repl = (
        'CrossPointFtPsramAllocator::logSummary("before-ft-done");\n'
        '    FT_Done_FreeType(g_FtLibrary);\n'
        '    CrossPointFtPsramAllocator::logSummary("after-ft-done");'
    )
    if "before-ft-done" not in text:
        if done_needle not in text:
            raise RuntimeError("Cannot patch OpenFontRender.cpp: FT_Done_FreeType anchor not found")
        text = text.replace(done_needle, done_repl, 1)

    if text != original:
        path.write_text(text, encoding="utf-8")
        return True
    return False


openfont_src = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "OpenFontRender" / "src" / "OpenFontRender.cpp"
changed = patch_file(openfont_src)
print("OpenFontRender FreeType PSRAM allocator patch " + ("applied: " if changed else "already present: ") + str(openfont_src))
