#!/bin/sh
# release_notes.sh TAG EXE... - print the GitHub Release notes for TAG: tools/ci/release-notes.md with the
# placeholders filled in (@FILES@: one line per exe with its size and SHA-256). An annotated tag's message
# becomes the "What's new" section (git tag -a v1.1 -m "Timer option, ..."); a lightweight tag gets none.
set -eu
[ $# -ge 2 ] || { echo "usage: $0 TAG EXE..." >&2; exit 2; }
tag=$1
shift
here=$(cd "$(dirname "$0")" && pwd)
commit=${GITHUB_SHA:-$(git -C "$here" rev-parse HEAD 2>/dev/null || echo unknown)}

notes=$(mktemp)
files=$(mktemp)
trap 'rm -f "$notes" "$files"' EXIT
for exe in "$@"; do
    [ -f "$exe" ] || { echo "$0: no such file: $exe" >&2; exit 1; }
    if command -v sha256sum >/dev/null 2>&1; then sha=$(sha256sum "$exe" | cut -d' ' -f1)
    else sha=$(shasum -a 256 "$exe" | cut -d' ' -f1); fi
    size=$(wc -c < "$exe" | tr -d ' ')
    echo "- \`$(basename "$exe")\`: $size bytes, SHA-256 \`$sha\`" >> "$files"
done
if [ "$(git -C "$here" cat-file -t "refs/tags/$tag" 2>/dev/null || true)" = tag ]; then
    {
        echo "### What's new"
        echo
        git -C "$here" for-each-ref --format='%(contents:subject)%0a%0a%(contents:body)' "refs/tags/$tag" |
            sed -e '/^-----BEGIN PGP SIGNATURE-----/,$d'
        echo
    } > "$notes"
fi

# @NOTES@ and @FILES@ are replaced by the files' contents (@NOTES@ is empty for a lightweight tag); the
# rest by plain values.
awk -v notes="$notes" -v files="$files" '
    /^@NOTES@$/ { while ((getline line < notes) > 0) print line; next }
    /^@FILES@$/ { while ((getline line < files) > 0) print line; next }
    { print }
' "$here/release-notes.md" |
    sed -e "s|@TAG@|$tag|g" -e "s|@COMMIT@|$commit|g"
