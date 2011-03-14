from distutils.core import setup, Extension

from numpy.distutils.misc_util import get_numpy_include_dirs

numpy_inc = get_numpy_include_dirs()

c_swig_module = Extension('_util',
						  sources = ['util_wrap.c' ],
						  include_dirs = numpy_inc +
						  [
							  '../qfits-an/include',
							  '../libkd',
							  '.'],
						  extra_objects = [
							  'libanfiles.a',
							  '../libkd/libkd.a',
							  'libanutils.a',
							  '../qfits-an/lib/libqfits.a',
							  ],
						  extra_compile_args=['-O0','-g'],
						  extra_link_args=['-O0', '-g'],
						  )

setup(name = 'Access to Astrometry.net utils in python',
      version = '1.0',
      description = '',
      author = 'Astrometry.net (Dustin Lang)',
      author_email = 'dstn@astro.princeton.edu',
      url = 'http://astrometry.net',
      py_modules = [ 'util' ],
	  ext_modules = [c_swig_module])
