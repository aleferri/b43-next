#!/bin/sh
# Checks out what a suite reads from one of the repositories this one links
# to, in the place paths.sh names: blobless and sparse, PATH... only. A
# sparse checkout already there gets PATH... added; any other is left as it is.
#
#   linked.sh TRACES|TRACE_TOOLS|B43_TOOLS PATH...
#
# LINKED_URL is where the repositories are cloned from, their names appended
# (https://github.com/aleferri).
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
. "$REPO/test/paths.sh"

[ $# -ge 2 ] || { echo "usage: $0 TRACES|TRACE_TOOLS|B43_TOOLS PATH..." >&2; exit 2; }
case $1 in
TRACES)      dir=$TRACES      name=b43-next-traces ;;
TRACE_TOOLS) dir=$TRACE_TOOLS name=b43-next-trace-tools ;;
B43_TOOLS)   dir=$B43_TOOLS   name=b43-tools ;;
*) echo "$0: $1: not a linked repository" >&2; exit 2 ;;
esac
shift

if [ ! -e "$dir" ]; then
	git clone --quiet --filter=blob:none --sparse \
		"${LINKED_URL:-https://github.com/aleferri}/$name" "$dir"
	git -C "$dir" sparse-checkout set "$@"
elif [ "$(git -C "$dir" config --bool core.sparseCheckout 2>/dev/null)" = true ]; then
	git -C "$dir" sparse-checkout add "$@"
fi
