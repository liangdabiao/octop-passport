#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    # 字库清单同理：新增界面文案后忘了重跑字库生成器，charset.txt 就会与源文件
    # 脱节。这一步要 fontTools 才能逐个码点核对母字体覆盖，而 CI 镜像不装
    # fontTools —— 缺依赖时明确跳过并说明，不假装通过。
    # 字库清单校验：P-M1 沿用 astock-quote 生成好的 GB2312 字库（op_font_*），
    # 自己的 gen_font（长文全字集）在 P-M2 落地时一并接回 --check。

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/op-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"

    # 本应用的纯逻辑层：WS 帧协议（Accept 锚定 RFC 6455 示例）、线缆 JSON
    # 助手（锚定 P-M0 真实 HarnessChunk 帧）、流式分页器（折行/跟随/截断）。
    # 它们刻意不依赖 ESP-IDF/LVGL，所以能在宿主机上直接跑。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_op_ws_proto.c main/op_ws_proto.c \
        -o "${test_dir}/test_op_ws_proto"
    "${test_dir}/test_op_ws_proto"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_op_wire.c main/op_wire.c \
        -o "${test_dir}/test_op_wire"
    "${test_dir}/test_op_wire"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_op_pager.c main/op_pager.c \
        -o "${test_dir}/test_op_pager"
    "${test_dir}/test_op_pager"

    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    # demo_*_runtime 四项需要 zig cc + --gc-sections 才能在宿主机链接
    # （家族既有口径：本机 MINGW 跳过，CI-only）。
    if [[ "$(uname -s)" != MINGW* ]]; then
        for demo in audio low_power ble wifi; do
            "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
                -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
                "tests/test_demo_${demo}_runtime.c" -Wl,--gc-sections \
                -o "${test_dir}/test_demo_${demo}_runtime"
            "${test_dir}/test_demo_${demo}_runtime"
        done
    fi
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; source D:/esp/idf-env-gen.sh first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/op-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/op-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
