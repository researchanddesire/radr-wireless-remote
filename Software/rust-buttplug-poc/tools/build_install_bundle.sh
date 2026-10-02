#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${LIBCLANG_PATH:-}" ]]; then
  export_candidates=()
  if [[ -n "${ESPUP_EXPORT_FILE:-}" ]]; then
    export_candidates+=("${ESPUP_EXPORT_FILE}")
  fi
  export_candidates+=("${HOME}/.cargo/esp-export.sh" "${HOME}/export-esp.sh")
  for export_candidate in "${export_candidates[@]}"; do
    if [[ -f "${export_candidate}" ]]; then
      # shellcheck disable=SC1090
      source "${export_candidate}"
      break
    fi
  done
fi

if [[ -d /opt/homebrew/opt/rustup/bin ]]; then
  export PATH="/opt/homebrew/opt/rustup/bin:${HOME}/.cargo/bin:${PATH}"
else
  export PATH="${HOME}/.cargo/bin:${PATH}"
fi

if [[ -z "${LIBCLANG_PATH:-}" ]]; then
  echo "Espressif Rust environment is unavailable; run espup install first." >&2
  exit 1
fi

exec python3 "${script_directory}/build_install_bundle.py" "$@"
