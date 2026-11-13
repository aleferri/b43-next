"""Imported by the test scripts in Python: the same places as paths.sh, and
the capture decoders' package, b43trace, on sys.path."""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
TRACES = os.environ.get('TRACES') or os.path.join(REPO, os.pardir, 'b43-next-traces')
TRACE_TOOLS = (os.environ.get('TRACE_TOOLS')
               or os.path.join(REPO, os.pardir, 'b43-next-trace-tools'))
B43_TOOLS = os.environ.get('B43_TOOLS') or os.path.join(REPO, os.pardir, 'b43-tools')

sys.path.insert(0, os.path.join(TRACE_TOOLS, 'src'))
