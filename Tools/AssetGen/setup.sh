#!/usr/bin/env bash
# AssetGen environment (Tools/AssetGen/README.md). Everything stays in Tools/AssetGen/.venv and HF_HOME;
# nothing touches the system Python or the shell's configuration.
#
#   Tools/AssetGen/setup.sh check                  what is there, what is missing (changes nothing)
#   Tools/AssetGen/setup.sh install                create .venv from uv.lock (pinned Python 3.12, mflux)
#   Tools/AssetGen/setup.sh login                  hf auth login into HF_HOME (skipped when a token is there)
#   Tools/AssetGen/setup.sh download               the pinned model revision into HF_HOME (skipped when there)
#   Tools/AssetGen/setup.sh uninstall [--dry-run] [--with-model] [--with-uv-cache] [--yes]
#
# HF_HOME: the environment's if set, else ~/AI/huggingface (config.json). The weights and the token there
# may be shared with other tools: uninstall leaves them unless --with-model, and never removes the token.
set -euo pipefail

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
	echo "AssetGen generates on Apple Silicon Macs only (MLX); this is $(uname -s) $(uname -m)." >&2
	echo "The post-processing tests run anywhere: uv run --project Tools/AssetGen python -m unittest discover -s Tools/AssetGen/tests" >&2
	exit 1
fi

here="$(cd "$(dirname "$0")" && pwd)"
venv="$here/.venv"

config_value() {
	/usr/bin/env python3 -c "import json,sys; c=json.load(open('$here/config.json')); print(eval('c' + sys.argv[1]))" "$1"
}

model_id="$(config_value '["model"]["id"]')"
revision="$(config_value '["model"]["revision"]')"
mflux_pin="$(config_value '["mflux_version"]')"
hf_default="$(config_value '["hf_home_default"]')"
export HF_HOME="${HF_HOME:-${hf_default/#\~/$HOME}}"
model_dir="$HF_HOME/hub/models--${model_id//\//--}"
snapshot="$model_dir/snapshots/$revision"
required_files=(transformer/diffusion_pytorch_model.safetensors text_encoder/model.safetensors.index.json
	vae/diffusion_pytorch_model.safetensors tokenizer/tokenizer.json)

ok() { printf '  \033[32mok\033[0m    %s\n' "$*"; }
bad() { printf '  \033[31mmissing\033[0m %s\n' "$*"; problems=$((problems + 1)); }
note() { printf '  \033[33mnote\033[0m  %s\n' "$*"; }
size_of() { if [ -e "$1" ]; then du -sh "$1" 2>/dev/null | cut -f1; else echo "-"; fi; }

confirm() {
	[ "${assume_yes:-0}" = 1 ] && return 0
	printf '%s [y/N] ' "$1"
	read -r answer
	[ "$answer" = y ] || [ "$answer" = Y ]
}

has_token() { [ -s "$HF_HOME/token" ]; }

snapshot_complete() {
	local file
	for file in "${required_files[@]}"; do
		[ -e "$snapshot/$file" ] || return 1
	done
}

model_size() {
	# The cache keeps blobs in a store shared between repositories: ask hf, which counts them right.
	if [ -x "$venv/bin/hf" ]; then
		"$venv/bin/hf" cache ls 2>/dev/null | awk -v id="model/$model_id" '$1 == id { print $2 }'
	elif [ -d "$model_dir" ]; then
		echo "?"
	fi
}

cmd_check() {
	problems=0
	echo "AssetGen check (changes nothing)"
	ok "Apple Silicon ($(sysctl -n machdep.cpu.brand_string), $(($(sysctl -n hw.memsize) / 1073741824)) GB)"
	if command -v uv >/dev/null; then ok "uv $(uv --version | cut -d' ' -f2)"; else bad "uv (brew install uv)"; fi
	if [ -x "$venv/bin/python" ]; then
		local installed
		installed="$("$venv/bin/python" -c 'from importlib.metadata import version; print(version("mflux"))' 2>/dev/null || echo none)"
		if [ "$installed" = "$mflux_pin" ]; then
			ok "environment: .venv, $("$venv/bin/python" --version), mflux $installed ($(size_of "$venv"))"
		else
			bad "mflux $mflux_pin in .venv (found: $installed): setup.sh install"
		fi
	else
		bad "environment .venv: setup.sh install"
	fi
	ok "HF_HOME $HF_HOME"
	if has_token; then ok "Hugging Face token (in HF_HOME)"; else bad "Hugging Face token: setup.sh login"; fi
	if snapshot_complete; then
		ok "model $model_id @ ${revision:0:12} ($(model_size), shared HF cache)"
	else
		bad "model $model_id @ ${revision:0:12}: setup.sh download"
	fi
	if [ -x "$venv/bin/python" ] && has_token; then
		if "$venv/bin/python" - "$model_id" "$revision" <<-'EOF' 2>/dev/null; then
			import sys
			from huggingface_hub import model_info
			model_info(sys.argv[1], revision=sys.argv[2], timeout=10)
		EOF
			ok "access to $model_id on Hugging Face"
		else
			note "could not confirm access to $model_id on Hugging Face (offline, or the token lacks access)"
		fi
	fi
	local free_gb
	free_gb=$(df -g "$HOME" | awk 'NR == 2 { print $4 }')
	if [ "$free_gb" -ge 20 ]; then ok "disk: $free_gb GB free"; else bad "disk: $free_gb GB free (the model needs ~16 GB, .venv ~1 GB)"; fi
	if pgrep -x PenumbraEditor >/dev/null; then note "PenumbraEditor is running: close it before generating on a 16 GB Mac"; fi
	if [ "$problems" -eq 0 ]; then echo "Ready."; else echo "$problems thing(s) missing."; exit 1; fi
}

cmd_install() {
	command -v uv >/dev/null || { echo "uv is needed (brew install uv)." >&2; exit 1; }
	echo "Creates $venv from uv.lock (Python 3.12 managed by uv, mflux $mflux_pin; about 1.1 GB)."
	confirm "Install?" || exit 1
	uv sync --project "$here" --frozen --extra gen
	"$venv/bin/python" -c 'from importlib.metadata import version; print("mflux", version("mflux"))'
}

cmd_login() {
	if has_token; then
		echo "A Hugging Face token is already in $HF_HOME: nothing to do."
		return
	fi
	[ -x "$venv/bin/hf" ] || { echo "Run setup.sh install first." >&2; exit 1; }
	echo "Paste your Hugging Face token when asked; it is stored in $HF_HOME/token, never in the repository."
	"$venv/bin/hf" auth login
}

cmd_download() {
	if snapshot_complete; then
		echo "$model_id @ $revision is already in $HF_HOME: nothing to do."
		return
	fi
	[ -x "$venv/bin/hf" ] || { echo "Run setup.sh install first." >&2; exit 1; }
	has_token || { echo "Run setup.sh login first." >&2; exit 1; }
	echo "Downloads $model_id @ $revision (about 16 GB) into $HF_HOME."
	confirm "Download?" || exit 1
	"$venv/bin/hf" download "$model_id" --revision "$revision"
}

cmd_uninstall() {
	local dry_run=0 with_model=0 with_uv=0
	for argument in "$@"; do
		case "$argument" in
			--dry-run) dry_run=1 ;;
			--with-model) with_model=1 ;;
			--with-uv-cache) with_uv=1 ;;
			--yes) assume_yes=1 ;;
			*) echo "Unknown option $argument" >&2; exit 2 ;;
		esac
	done

	local uv_cache="" uv_python=""
	if command -v uv >/dev/null; then
		uv_cache="$(uv cache dir 2>/dev/null || true)"
		uv_python="$(find "$(uv python dir 2>/dev/null)" -maxdepth 1 -type d -name 'cpython-3.12.*' 2>/dev/null | head -n 1)"
	fi
	local size
	size="$(model_size)"

	line() { printf '  %6s  %s%s\n' "${1:--}" "$2" "${3:+  ($3)}"; }
	echo "Removes:"
	line "$(size_of "$venv")" "$venv"
	line "$(size_of "$here/out")" "$here/out"
	[ "$with_model" = 1 ] && line "$size" "$model_id in $HF_HOME" "shared with your other Hugging Face tools"
	if [ "$with_uv" = 1 ]; then
		[ -n "$uv_cache" ] && line "$(size_of "$uv_cache")" "$uv_cache" "uv's download cache, shared by every uv project"
		[ -n "$uv_python" ] && line "$(size_of "$uv_python")" "$uv_python" "uv's Python 3.12"
	fi
	echo "Keeps:"
	[ "$with_model" = 1 ] || line "$size" "$model_id in $HF_HOME" "shared with your other Hugging Face tools: --with-model"
	if [ "$with_uv" = 0 ]; then
		[ -n "$uv_cache" ] && line "$(size_of "$uv_cache")" "$uv_cache" "shared by every uv project: --with-uv-cache"
		[ -n "$uv_python" ] && line "$(size_of "$uv_python")" "$uv_python" "uv's Python 3.12: --with-uv-cache"
	fi
	line "" "$HF_HOME/token" "the Hugging Face token: never removed here, see README.md"
	[ "$dry_run" = 1 ] && return

	if [ "$with_model" = 1 ]; then
		echo "Warning: the model is in the shared Hugging Face cache ($HF_HOME); every tool using $model_id will have to download it again."
	fi
	confirm "Remove everything listed under Removes?" || exit 1
	if [ "$with_model" = 1 ]; then
		[ -x "$venv/bin/hf" ] || { echo "Removing the model needs .venv's hf: run it before removing .venv, or use hf cache rm yourself." >&2; exit 1; }
		"$venv/bin/hf" cache rm "model/$model_id" --yes
	fi
	if [ "$with_uv" = 1 ]; then
		uv cache clean
		uv python uninstall 3.12
	fi
	for path in "$venv" "$here/out"; do
		if [ -e "$path" ]; then rm -rf "$path"; echo "removed $path"; fi
	done
}

command="${1:-check}"
[ $# -gt 0 ] && shift
case "$command" in
	check) cmd_check ;;
	install) cmd_install ;;
	login) cmd_login ;;
	download) cmd_download ;;
	uninstall) cmd_uninstall "$@" ;;
	*) sed -n '2,12p' "$0"; exit 2 ;;
esac
