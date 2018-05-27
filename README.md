# WARNING: EXPERIMENTAL

- This is a _toy project_ to learn about systemd, the new gpio
  character device, E1.31, C++, and scons.

- The quality of this piece of software is embarrassingly sub-par.

# e131_blinkt

``e131_blinkt`` receives ``E1.31`` DMX channel data from the network and relays that to a ``Blinkt!``

Supports all standard ``E1.31`` features, including:

- Source packet sequence validation
- Source priority arbitration
    - In the event where two sources share the same priority level, then the LTP principle is used
      to decide which source's data is output.
- Source network data loss detection
    - Detects _transmission terminated_ flag
    - Implements source transmission timeout
        
Extended ``E1.31`` features are _not supported_.

# Dependencies

- Build & run time dependencies:

```
libgpiod
libconfig
libe131
systemd
docopt
glibc
```

- Build-only dependencies:


```
C++ compiler with support for c++17
```

# Build
```
git clone https://github.com/shenghaoyang/e131_blinkt.git
scons
```

- Built binary will be found under ``Release``.

# Install

```
git clone https://github.com/shenghaoyang/e131_blinkt.git
scons install
```

- Installs under ``/usr`` by default.

- See ``scons --help`` for more information.

# Uninstall

```
scons -c install
```

# Daemon configuration

- The configuration file is installed in 
  ``/etc/e131_blinkt/e131_blinkt.conf``. See inline
  comments on how to configure the program. ``e131_blinkt`` uses
  ``libconfig`` for configuration parsing.

- There is a ``systemd`` service file included. Enable and start
  ``e131_blinkt`` through:
  ``# systemctl enable --now e131_blinkt@/dev/gpiochip0.service``.
  Replace ``/dev/gpiochip0`` with your desired GPIO character 
  device.
  
# License

- Licensed under the MIT license. See ``LICENSE`` for details.
