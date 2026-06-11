#!/usr/bin/env bash
# =============================================================================
# build.sh — EDK2 UEFI for Rockchip RK3326 (PX30)
#
# Linux-only. Requires: gcc-aarch64-linux-gnu, python3, uuid-dev
#
# Usage:
#   bash build.sh DEBUG              # debug build
#   bash build.sh RELEASE            # release build
#   bash build.sh clean              # wipe Build/ and Conf/
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2="$SCRIPT_DIR/edk2"
WSDIR="$SCRIPT_DIR"
LOG="$WSDIR/build.log"
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; CYN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GRN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YLW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
step()  { echo -e "\n${CYN}══ $* ══${NC}"; }

# ── Arguments ────────────────────────────────────────────────────────────
TARGET="${1:-DEBUG}"

[[ "$TARGET" =~ ^(DEBUG|RELEASE)$ ]] || {
    if [ "$TARGET" = "clean" ]; then
        info "Cleaning Build/ and Conf/ ..."
        rm -rf "$WSDIR/Build" "$WSDIR/Conf" "$WSDIR/build.log"
        info "Done."
        exit 0
    fi
    error "Usage: $0 [DEBUG|RELEASE|clean]"
}

DSC_FILE="edk2-rockchip/Platform/Rockchip/RK3326Evb/RK3326Evb.dsc"
PLATFORM_NAME="RK3326EVB"
info "Platform: ${PLATFORM_NAME} | Target: $TARGET"

# ── STEP 1: Environment ──────────────────────────────────────────────────
step "1/5  Check Environment"

HOST_ARCH=$(uname -m)
HOST_OS=$(uname -s)

if [ "$HOST_OS" != "Linux" ]; then
    error "This build script is Linux-only. Current OS: $HOST_OS\n  Use OrbStack or a Linux VM."
fi

info "Host: $HOST_ARCH | GCC: $(gcc --version 2>/dev/null | head -1) | Jobs: $(nproc)"

# Cross-compiler setup
if [ "$HOST_ARCH" = "aarch64" ]; then
    export GCC_AARCH64_PREFIX=""
    info "Mode: Native AArch64 build"
elif [ "$HOST_ARCH" = "x86_64" ]; then
    command -v aarch64-linux-gnu-gcc &>/dev/null || \
        error "Need aarch64-linux-gnu-gcc\n  sudo apt-get install gcc-aarch64-linux-gnu"
    export GCC_AARCH64_PREFIX="aarch64-linux-gnu-"
    info "Mode: Cross-compile x86_64 -> AArch64"
else
    error "Unsupported host arch: $HOST_ARCH"
fi

# ── STEP 2: BaseTools ────────────────────────────────────────────────────
step "2/5  Compile BaseTools"

[ ! -d "$EDK2/BaseTools" ] && error "edk2/BaseTools not found. Clone edk2 first."

BT_BIN="$EDK2/BaseTools/Source/C/bin/GenFw"

info "Building BaseTools for Linux $HOST_ARCH..."
make -C "$EDK2/BaseTools/Source/C" clean 2>&1 | tail -1
make -C "$EDK2/BaseTools/Source/C" -j"$(nproc)" 2>&1 | tail -3
[ -f "$BT_BIN" ] || error "BaseTools build failed"
info "BaseTools OK ✓"

# ── STEP 3: Init submodules ──────────────────────────────────────────────
step "3/5  Init Submodules"

if [ -f "$EDK2/.gitmodules" ]; then
    cd "$EDK2"
    for sub in \
        "MdeModulePkg/Library/BrotliCustomDecompressLib/brotli" \
        "MdePkg/Library/MipiSysTLib/mipisyst" \
        "MdePkg/Library/BaseFdtLib/libfdt" \
        "CryptoPkg/Library/OpensslLib/openssl"; do
        [ -d "$sub" ] && [ -z "$(ls -A "$sub" 2>/dev/null)" ] && {
            info "Init: $sub"
            git submodule update --init --depth=1 "$sub" 2>&1 | tail -2 || warn "Init $sub failed"
        }
    done
    cd "$SCRIPT_DIR"
fi
info "Submodules OK ✓"

# ── STEP 4: Setup workspace ──────────────────────────────────────────────
step "4/5  Setup Build Environment"

export EDK_TOOLS_PATH="$EDK2/BaseTools"
export WORKSPACE="$WSDIR"
export CONF_PATH="$WSDIR/Conf"
export PACKAGES_PATH="$EDK2:$WSDIR/edk2-rockchip"
export PYTHONPATH="$EDK2/BaseTools/Source/Python"
export PATH="$EDK2/BaseTools/BinWrappers/PosixLike:$EDK2/BaseTools/Source/C/bin:$PATH"

# Create Conf directory
mkdir -p "$WSDIR/Conf"
cp "$EDK2/BaseTools/Conf/target.template"     "$WSDIR/Conf/target.txt"     2>/dev/null || true
cp "$EDK2/BaseTools/Conf/tools_def.template"  "$WSDIR/Conf/tools_def.txt"  2>/dev/null || true
cp "$EDK2/BaseTools/Conf/build_rule.template" "$WSDIR/Conf/build_rule.txt" 2>/dev/null || true

# Patch tools_def.txt for GCC 10-13 compatibility (from RK3576 pattern)
info "Patching tools_def.txt for GCC compatibility..."
python3 - "$WSDIR/Conf/tools_def.txt" << 'PYEOF'
import re, sys
path = sys.argv[1]
with open(path) as f:
    lines = f.readlines()

GCC13 = (
    " -Wno-implicit-function-declaration"
    " -Wno-error=implicit-function-declaration"
    " -Wno-error=incompatible-pointer-types"
    " -Wno-error=int-conversion"
    " -Wno-stringop-overflow"
    " -Wno-dangling-pointer"
    " -Wno-use-after-free"
    " -Wno-array-bounds"
    " -Wno-maybe-uninitialized"
    " -Wno-error=maybe-uninitialized"
    " -Wno-uninitialized"
)
LTO_RE    = re.compile(r'-flto\s+-Os\s+-L\S+\s+-llto-aarch64\s+-Wl,-plugin-opt=-pass-through=-llto-aarch64\s+-Wno-lto-type-mismatch')
SP_RE     = re.compile(r'-fstack-protector(?!-off|-ra)')
SGUARD_RE = re.compile(r'-mstack-protector-guard=\S+')
CC_LTO_RE = re.compile(r'\s+-flto\b')

p1 = p2 = p3 = 0
out = []
for line in lines:
    s = line.rstrip('\r\n')
    if re.match(r'^(RELEASE|DEBUG|NOOPT)_GCC[0-9]*_AARCH64_CC_FLAGS\s*=', s):
        if GCC13.split()[0] not in s:
            s += GCC13; p1 += 1
        if '-flto' in s:
            s = CC_LTO_RE.sub('', s); p2 += 1
    if re.match(r'^(RELEASE|DEBUG|NOOPT)_GCC[0-9]*_AARCH64_DLINK_FLAGS\s*=', s):
        if LTO_RE.search(s):
            s = LTO_RE.sub('-Os', s); p2 += 1
        if '-flto' in s:
            s = CC_LTO_RE.sub('', s); p2 += 1
    if '_AARCH64_CC_FLAGS' in s and 'DEFINE' not in s and '=' in s:
        b = s
        s = SP_RE.sub('-fno-stack-protector', s)
        s = SGUARD_RE.sub('', s)
        if '-fno-stack-protector' not in s:
            s = s.rstrip() + ' -fno-stack-protector'
        s = re.sub(r'  +', ' ', s).rstrip()
        if s != b: p3 += 1
    out.append(s + '\n')

with open(path, 'w') as f:
    f.writelines(out)
print(f"tools_def.txt patched: GCC13 warnings={p1}, no-LTO={p2}, no-SSP={p3}")
PYEOF
info "tools_def.txt patched ✓"

# Source edksetup
set +eu
source "$EDK2/edksetup.sh" BaseTools 2>/dev/null || true
set -euo pipefail
export WORKSPACE="$WSDIR"

info "GenFw: $(which GenFw 2>/dev/null || echo 'NOT FOUND')"

# ── STEP 5: Build ────────────────────────────────────────────────────────
step "5/5  Build EDK2 for RK3326"

warn "Starting EDK2 build (GCC/AARCH64/$TARGET)..."

set +e
build -s -n "$(nproc)" -a AARCH64 -t GCC \
      -p "$DSC_FILE" \
      -b "$TARGET" \
      -D FIRMWARE_VER="rk3326-${PLATFORM_NAME}-v0.1" \
      2>&1 | tee "$LOG"
BUILD_EXIT="${PIPESTATUS[0]}"
set -e

if [ "$BUILD_EXIT" -ne 0 ]; then
    echo ""
    error "Build FAILED (exit $BUILD_EXIT)\nErrors:\n$(grep -E 'error [0-9A-F]+:|\.c:[0-9]+:.*error:|F001:|F002:|F003:|F015:|4000:|fatal error:' "$LOG" 2>/dev/null | tail -8 | sed 's/^/  /')\n\nFull log: $LOG"
fi

# ── Output ───────────────────────────────────────────────────────────────
FV="$WSDIR/Build/${PLATFORM_NAME}/${TARGET}_GCC/FV/BL33_AP_UEFI.Fv"
FD="$WSDIR/Build/${PLATFORM_NAME}/${TARGET}_GCC/FV/NOR_FLASH_IMAGE.fd"

if [ -f "$FD" ]; then
    FD_KB=$(( $(stat -c%s "$FD" 2>/dev/null || stat -f%z "$FD" 2>/dev/null || echo 0) / 1024 ))
    echo ""
    echo -e "${GRN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GRN}║    RK3326 EDK2 UEFI 编译完成!                            ║${NC}"
    echo -e "${GRN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "  FV: ${CYN}$FV${NC}"
    echo -e "  FD: ${CYN}$FD${NC}  ($FD_KB KB)"
    echo ""
fi
