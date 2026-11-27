#!/usr/bin/env bash
# AudioGen environment (Tools/AudioGen/README.md). Everything stays in Tools/AudioGen/.venv and HF_HOME;
# nothing touches the system Python or the shell's configuration.
#
#   Tools/AudioGen/setup.sh check                  what is there, what is missing (changes nothing)
#   Tools/AudioGen/setup.sh install                create .venv from uv.lock (pinned Python 3.12, stable-audio-3, PyTorch)
#   Tools/AudioGen/setup.sh login                  hf auth login into HF_HOME (skipped when a token is there)
#   Tools/AudioGen/setup.sh download               the pinned model revision into HF_HOME (skipped when there)
#   Tools/AudioGen/setup.sh uninstall [--dry-run] [--with-model] [--yes]
#
# HF_HOME: the environment's if set, else ~/AI/huggingface (config.json). The weights and the token there
# may be shared with other tools (AssetGen): uninstall leaves them unless --with-model, and never removes the token.
set -euo pipefail

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
	echo "AudioGen generates on Apple Silicon Macs only; this is $(uname -s) $(uname -m)." >&2
	echo "The checks and their tests run anywhere: uv run --project Tools/AudioGen python -m unittest discover -s Tools/AudioGen/tests" >&2
	exit 1
fi

here="$(cd "$(dirname "$0")" && pwd)"
venv="$here/.venv"

config_value() {
	/usr/bin/env python3 -c "import json,sys; c=json.load(open('$here/config.json')); print(eval('c' + sys.argv[1]))" "$1"
}

model_id="$(config_value '["model"]["id"]')"
revision="$(config_value '["model"]["revision"]')"
hf_default="$(config_value '["hf_home_default"]')"
export HF_HOME="${HF_HOME:-${hf_default/#\~/$HOME}}"
model_dir="$HF_HOME/hub/models--${model_id//\//--}"
snapshot="$model_dir/snapshots/$revision"
required_files=(model.safetensors model_config.json t5gemma-b-b-ul2/config.json)
download_files=()
while IFS= read -r pattern; do download_files+=(--include "$pattern"); done \
	< <(/usr/bin/env python3 -c "import json; [print(p) for p in json.load(open('$here/config.json'))['download_files']]")

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
	if [ -x "$venv/bin/hf" ]; then
		"$venv/bin/hf" cache ls 2>/dev/null | awk -v id="model/$model_id" '$1 == id { print $2 }'
	elif [ -d "$model_dir" ]; then
		echo "?"
	fi
}

cmd_check() {
	problems=0
	echo "AudioGen check (changes nothing)"
	ok "Apple Silicon ($(sysctl -n machdep.cpu.brand_string), $(($(sysctl -n hw.memsize) / 1073741824)) GB)"
	if command -v uv >/dev/null; then ok "uv $(uv --version | cut -d' ' -f2)"; else bad "uv (brew install uv)"; fi
	if [ -x "$venv/bin/python" ]; then
		local installed
		installed="$("$venv/bin/python" -c 'from importlib.metadata import version as v; print(v("stable-audio-3"), "torch", v("torch"))' 2>/dev/null || echo none)"
		if [ "$installed" != none ]; then
			ok "environment: .venv, $("$venv/bin/python" --version), stable-audio-3 $installed ($(size_of "$venv"))"
		else
			bad "stable-audio-3 in .venv: setup.sh install"
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
	local free_gb
	free_gb=$(df -g "$HOME" | awk 'NR == 2 { print $4 }')
	if [ "$free_gb" -ge 10 ]; then ok "disk: $free_gb GB free"; else bad "disk: $free_gb GB free (the model needs ~3.5 GB, .venv ~0.6 GB)"; fi
	if pgrep -x PenumbraEditor >/dev/null; then note "PenumbraEditor is running: close it before generating on a 16 GB Mac"; fi
	if [ "$problems" -eq 0 ]; then echo "Ready."; else echo "$problems thing(s) missing."; exit 1; fi
}

cmd_install() {
	command -v uv >/dev/null || { echo "uv is needed (brew install uv)." >&2; exit 1; }
	echo "Creates $venv from uv.lock (Python 3.12 managed by uv, stable-audio-3 at a pinned commit, PyTorch; about 0.6 GB)."
	confirm "Install?" || exit 1
	uv sync --project "$here" --frozen --extra gen
}

cmd_login() {
	if has_token; then
		echo "A Hugging Face token is already in $HF_HOME: nothing to do."
		return
	fi
	[ -x "$venv/bin/hf" ] || { echo "Run setup.sh install first." >&2; exit 1; }
	echo "Paste your Hugging Face token when asked; it is stored in $HF_HOME/token, never in the repository."
	echo "The model is gated: accept its terms on huggingface.co/$model_id with the same account first."
	"$venv/bin/hf" auth login
}

cmd_download() {
	if snapshot_complete; then
		echo "$model_id @ $revision is already in $HF_HOME: nothing to do."
		return
	fi
	[ -x "$venv/bin/hf" ] || { echo "Run setup.sh install first." >&2; exit 1; }
	has_token || { echo "Run setup.sh login first." >&2; exit 1; }
	echo "Downloads $model_id @ $revision (about 3.5 GB) into $HF_HOME."
	confirm "Download?" || exit 1
	"$venv/bin/hf" download "$model_id" --revision "$revision" "${download_files[@]}"
}

cmd_uninstall() {
	local dry_run=0 with_model=0
	for argument in "$@"; do
		case "$argument" in
			--dry-run) dry_run=1 ;;
			--with-model) with_model=1 ;;
			--yes) assume_yes=1 ;;
			*) echo "Unknown option $argument" >&2; exit 2 ;;
		esac
	done
	local size
	size="$(model_size)"
	line() { printf '  %6s  %s%s\n' "${1:--}" "$2" "${3:+  ($3)}"; }
	echo "Removes:"
	line "$(size_of "$venv")" "$venv"
	line "$(size_of "$here/out")" "$here/out" "every series, listened to or not"
	[ "$with_model" = 1 ] && line "$size" "$model_id in $HF_HOME" "shared with your other Hugging Face tools"
	echo "Keeps:"
	[ "$with_model" = 1 ] || line "$size" "$model_id in $HF_HOME" "shared with your other Hugging Face tools: --with-model"
	line "" "uv's cache and Python 3.12" "shared with AssetGen and every uv project: Tools/AssetGen/setup.sh uninstall --with-uv-cache"
	line "" "$HF_HOME/token" "the Hugging Face token: never removed here"
	[ "$dry_run" = 1 ] && return

	confirm "Remove everything listed under Removes?" || exit 1
	if [ "$with_model" = 1 ]; then
		[ -x "$venv/bin/hf" ] || { echo "Removing the model needs .venv's hf: run it before removing .venv, or use hf cache rm yourself." >&2; exit 1; }
		"$venv/bin/hf" cache rm "model/$model_id" --yes
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
