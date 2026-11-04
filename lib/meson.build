subdir('backend')
subdir('marco')
subdir('fx')
subdir('bg')
if get_option('xwayland')
  subdir('xwm')
endif
have_xsmp = get_option('xwayland') and dependency('sm', required: false).found() and \
  dependency('ice', required: false).found()
if have_xsmp
  subdir('xsmp')
endif
