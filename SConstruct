# Scons build file for e131_blinkt
import os

# User interface declarations
Help(
"""
SConstruct for e131_blinkt

Type:
    'scons release' to build the release version of the program.
    'scons debug'   to build the development version of the program.
    'scons all'     to build all targets.
    'scons install' to install e131_blinkt.
    'scons -c install' to uninstall e131_blinkt.

Command line build variables:
    'scons install':
        [DESTDIR=DIRECTORY]  root directory to install files under 
                             [default: /]
    
By default, the release target is built.
"""
)

# Library dependencies and headers
libs = {
    'e131'    : ('C', ['e131.h']),
    'systemd' : ('C', ['systemd/sd-event.h', 'systemd/sd-journal.h',
                      'systemd/sd-daemon.h']),
    'config++': ('C++', ['libconfig.h++']),
    'docopt'  : ('C++', ['docopt/docopt.h']),
    'gpiodcxx': ('C++', ['gpiod.hpp'])
}

# Header dependencies
headers_c = (
    'unistd.h',
    'endian.h',
    'sys/stat.h',
    'sys/types.h',
    'fcntl.h'
)

headers_cxx = (
    'system_error',
    'iostream',
    'cstdlib',
    'cstdint',
    'memory',
    'map',
    'stdexcept',
    'array',
    'iostream',
    'iterator',
    'queue',
    'string',
    'cstdint',
    'memory',
    'limits',
    'functional'
)



# Create build environment
common_env = Environment()

# Check local environment for dependencies
conf = Configure(common_env)

# Check libraries
for lib in libs:
    if not conf.CheckLib(lib):
        print('Library {lib} required but not found'.format(lib=lib))
        exit(1)

# Check header files in system
headers = []
for lib in libs:
    language, lib_headers = libs[lib]
    headers.extend([(language, header) for header in lib_headers])
headers.extend([('C', header) for header in headers_c])
headers.extend([('C++', header) for header in headers_cxx])
headers.sort()

for language, header in headers:
    if not conf.CheckHeader(header, language=language):
        print('Header {header} required but not found'.format(header=header))
        exit(1)

conf.Finish()

# Build directives
common_env.Append(CFLAGS=os.environ.setdefault('CFLAGS', ''))
common_env.Append(CPPFLAGS=os.environ.setdefault('CPPFLAGS', ''))
common_env.Append(CXXFLAGS=os.environ.setdefault('CXXFLAGS', ''))
common_env.Append(LDFLAGS=os.environ.setdefault('LDFLAGS', ''))
common_env.Append(CPPPATH='src')
common_env.Append(CXXFLAGS='-std=c++17')
common_env.ParseConfig('pkg-config --cflags --libs libsystemd')
common_env.ParseConfig('pkg-config --cflags --libs libconfig++')
common_env.ParseConfig('pkg-config --cflags --libs docopt')
common_env.ParseConfig('pkg-config --cflags --libs libgpiodcxx')

debug = common_env.Clone()
release = common_env.Clone()

debug.Append(CXXFLAGS='-O0 -g -DDEBUG')
release.Append(CXXFLAGS='-O2 -flto') 

VariantDir('Debug', 'src')
VariantDir('Release', 'src')

debug_program = debug.Program('Debug/e131_blinkt', Glob('Debug/*.cpp'))
release_program = release.Program('Release/e131_blinkt', Glob('Release/*.cpp'))

Alias('debug', debug_program)
Alias('release', release_program)
Alias('all', [debug_program, release_program])
Default(release_program)

# Install directives 
files = {
    '/usr/bin/e131_blinkt': ('Release/e131_blinkt', 0755),
    '/etc/e131_blinkt/e131_blinkt.conf': ('e131_blinkt.conf', 0644),
    '/usr/share/factory/etc/e131_blinkt/e131_blinkt.conf': 
        ('e131_blinkt.conf', 0644), 
    '/usr/lib/systemd/system/e131_blinkt@.service': 
        ('e131_blinkt@.service', 0644)
}

destdir = ARGUMENTS.setdefault('DESTDIR', '/')

install_nodes = []
for target, (source, mode) in files.items():
    install_nodes.append(
        release.Command(os.path.join(destdir, target), source, 
                        [Copy('$TARGET', '$SOURCE'),
                         Chmod('$TARGET', mode)]))

release.Alias('install', install_nodes)