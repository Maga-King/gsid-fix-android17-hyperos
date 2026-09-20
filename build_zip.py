#!/usr/bin/env python3
"""Compatibility entry point for the adapted Android 17 packager."""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).with_name('package_android17.py')), run_name='__main__')
