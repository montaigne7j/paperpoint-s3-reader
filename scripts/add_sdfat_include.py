Import("env")

from os.path import isdir, join


sdfat_src = join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env.subst("$PIOENV"),
    "SdFat",
    "src",
)

if not isdir(sdfat_src):
    raise RuntimeError(
        "SdFat include directory was not found: "
        + sdfat_src
    )

# 對主程式與所有 lib/ 下的本地 library 生效。
env.Append(
    CPPPATH=[
        sdfat_src,
    ]
)

print(
    "SdFat include path added: "
    + sdfat_src
)