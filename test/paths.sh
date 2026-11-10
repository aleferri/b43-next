# Sourced by the test scripts: where they find what lives outside this
# repository. REPO is the root of this one.
#
#   TRACES       the captures, one directory per board (router-data layout)
#   TRACE_TOOLS  the capture decoders (reverse-tools layout)
#   B43_TOOLS    b43-tools, for the virtual chip
TRACES=${TRACES:-$REPO/../b43-next-traces}
TRACE_TOOLS=${TRACE_TOOLS:-$REPO/../b43-next-trace-tools}
B43_TOOLS=${B43_TOOLS:-$REPO/../b43-tools}
export TRACES TRACE_TOOLS B43_TOOLS
