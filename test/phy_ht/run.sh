#!/bin/sh
# check.sh and gates.sh on the linked repositories: the R6400 v1's captures
# and the decoders are checked out where ../paths.sh says, unless they are
# there already, then ht-trace is built and both run. K and KG, if given,
# from the environment, as make takes them.
#
#   ./run.sh [-v]        -v goes to check.sh
set -eu
cd "$(dirname "$0")"

../linked.sh TRACES bcm4331-rev2/r6400v1
../linked.sh TRACE_TOOLS src
make ht-trace

status=0
./check.sh "$@" || status=$?
./gates.sh --min-block 8 --gap-tol 2
exit $status
