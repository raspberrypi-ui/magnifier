project ('mage-rpi','c')

sources = files ('mage.c')

atspi = dependency ('atspi-2')
xcomp = dependency ('xcomposite') 
xrend = dependency ('xrender')

deps = [ atspi, xcomp, xrend ]

add_project_link_arguments('-lXext', '-lX11', '-lpthread', language : 'c' )

executable (meson.project_name(), sources, dependencies: deps, install: true)
