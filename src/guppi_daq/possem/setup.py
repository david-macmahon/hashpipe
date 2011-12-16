#!/usr/bin/env python

"""POSIX Semaphore Package

Python package for interacting with POSIX semaphores.
"""

from distutils.core import setup, Extension
import os
import sys

srcdir = '.'
doclines = __doc__.split("\n")

setup(
    name        = 'possem'
  , version     = '0.1'
  , py_modules  = ['possem']
  , maintainer = "NRAO"
  , ext_modules=[Extension('_possem',
                           [os.path.join(srcdir, 'possem.i')])]
  # , maintainer_email = ""
  # , url = ""
  , license = "http://www.gnu.org/copyleft/gpl.html"
  , platforms = ["any"]
  , description = doclines[0]
  , long_description = "\n".join(doclines[2:])
  )
