#!/usr/bin/env bash
set -euo pipefail

REMOTE_NAME="${REMOTE_NAME:-origin}"
REMOTE_URL="${REMOTE_URL:-}"
BRANCH="${BRANCH:-}"

die(){ echo "ERROR: $*" >&2; exit 1; }

need_repo(){
	git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not inside a git repo"
}

cur_branch(){
	git rev-parse --abbrev-ref HEAD
}

ensure_remote(){
	if git remote get-url "$REMOTE_NAME" >/dev/null 2>&1; then
		return 0
	fi

	if [[ -z "$REMOTE_URL" ]]; then
		echo "Remote '$REMOTE_NAME' not set."
		read -r -p "Enter remote URL (e.g. git@github.com:you/uzenet-master.git): " REMOTE_URL
		[[ -n "$REMOTE_URL" ]] || die "no remote URL provided"
	fi

	git remote add "$REMOTE_NAME" "$REMOTE_URL"
}

# Refuse to commit/push if any of these are staged or tracked
FORBIDDEN_RE='(^|/)(\.env|id_rsa|id_ed25519)$|(\.pem$|\.key$|\.p12$|\.kdbx$|api-key|secret|/data/|/secrets/|/certs/)'

forbidden_tracked(){
	# checks staged + already-tracked
	local staged tracked
	staged="$(git diff --cached --name-only || true)"
	tracked="$(git ls-files || true)"
	echo "$staged"$'\n'"$tracked" | grep -E -i "$FORBIDDEN_RE" >/dev/null 2>&1
}

show_forbidden(){
	local staged tracked
	staged="$(git diff --cached --name-only || true)"
	tracked="$(git ls-files || true)"
	{ echo "$staged"; echo "$tracked"; } | grep -E -i "$FORBIDDEN_RE" | sort -u || true
}

main(){
	need_repo
	ensure_remote

	if [[ -z "$BRANCH" ]]; then
		BRANCH="$(cur_branch)"
	fi

	echo
	echo "Repo:   $(pwd)"
	echo "Remote: $REMOTE_NAME -> $(git remote get-url "$REMOTE_NAME")"
	echo "Branch: $BRANCH"
	echo

	# Show status before staging
	git status

	# Stage everything (honors .gitignore)
	echo
	echo "Staging changes..."
	git add -A

	# Safety check AFTER staging
	if forbidden_tracked; then
		echo
		echo "Refusing: forbidden secret-like files detected (staged or tracked):"
		show_forbidden
		echo
		echo "Fix: remove them from the repo, move them elsewhere, and/or add to .gitignore."
		echo "If already tracked: git rm --cached <file>  (then commit)"
		exit 1
	fi

	# Commit if there are staged changes
	if [[ -n "$(git diff --cached --name-only)" ]]; then
		local msg="${1:-}"
		if [[ -z "$msg" ]]; then
			read -r -p "Commit message: " msg
			[[ -n "$msg" ]] || msg="Update uzenet-master"
		fi

		echo
		echo "Committing..."
		git commit -m "$msg"
	else
		echo
		echo "No staged changes to commit."
	fi

	echo
	echo "Pushing..."
	git push "$REMOTE_NAME" "$BRANCH"

	echo "Done."
}

main "$@"
