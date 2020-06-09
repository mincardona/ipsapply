project('ipsapply', 'c', default_options : ['c_std=c89', 'werror=true'])

srcs = [
    'src/ipsapply.c',
    'src/options.c'
]

cargs = ['-pedantic-errors', '-Wall', '-Wextra', '-fno-strict-aliasing']

config_data = configuration_data()
config_data.set('version_str', '"0.0.1"')
config_data.set('version_major', 0)
config_data.set('version_minor', 0)
config_data.set('version_patch', 1)
configure_file(
    input : 'src/config_ipsapply.h.in',
    output : 'config_ipsapply.h',
    configuration : config_data
)
config_inc = include_directories('.')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

executable('ipsa',
    sources : srcs,
    c_args : cargs,
    include_directories : [config_inc],
    dependencies : m_dep
)
