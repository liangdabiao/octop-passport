import io

f = "tools/validate.sh"
s = io.open(f, encoding="utf-8").read()

bs = chr(92)
old_lines = [
    '    for demo in audio low_power ble wifi; do',
    f'        "${{CC:-cc}}" -std=c11 -Wall -Wextra -Werror {bs}',
    '            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain {bs}',
    f'            "tests/test_demo_${{demo}}_runtime.c" -Wl,--gc-sections {bs}',
    '            -o "${test_dir}/test_demo_${demo}_runtime"',
    '        "${test_dir}/test_demo_${demo}_runtime"',
    '    done',
]
old = "\n".join(l.replace("{bs}", bs) for l in old_lines)
assert old in s, "demo loop not found"

new_lines = [
    '    # demo_*_runtime 四项需要 zig cc + --gc-sections 才能在宿主机链接',
    '    # （家族既有口径：本机 MINGW 跳过，CI-only）。',
    '    if [[ "$(uname -s)" != MINGW* ]]; then',
    '        for demo in audio low_power ble wifi; do',
    f'            "${{CC:-cc}}" -std=c11 -Wall -Wextra -Werror {bs}',
    '                -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain {bs}',
    f'                "tests/test_demo_${{demo}}_runtime.c" -Wl,--gc-sections {bs}',
    '                -o "${test_dir}/test_demo_${demo}_runtime"',
    '            "${test_dir}/test_demo_${demo}_runtime"',
    '        done',
    '    fi',
]
new = "\n".join(l.replace("{bs}", bs) for l in new_lines)
s = s.replace(old, new)
io.open(f, "w", encoding="utf-8", newline="\n").write(s)
print("demo loop guarded")
