# WARNING: EXPERIMENTAL

- This is a _toy project_ to learn about systemd, 
  E1.31, C++, and scons.

- The quality of this piece of software is embarrassingly sub-par.

# e131_blinkt

``e131_blinkt`` receives ``E1.31`` DMX channel data from the network and relays that to a ``Blinkt!``

Since the ``Blinkt!`` is not connected to any hardware SPI lines, a bit-banged SPI bus provided by the kernel using ``spi-gpio`` is necessary. As far as I know, this module is enabled on the 32-bit & 64-bit builds of Arch Linux ARM for both the Raspberry Pi (+Zero), Raspberry Pi 2 and Raspberry Pi 3.

A device tree overlay is required to inform the kernel about our desire to instantiate such a bit-banged SPI line. A reference device tree overlay source file is provided in ``blinkt_overlay.dts``. Use ``dtc`` to compile the overlay into a device tree blob.

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

- The configuration file is installed in ``/etc/e131_blinkt/e131_blinkt.conf``. See inline comments on how to configure the program. ``e131_blinkt`` uses ``libconfig`` for configuration parsing.

- There is a ``systemd`` service file included. Enable and start ``e131_blinkt`` through: 
``# systemctl enable --now e131_blinkt@spidev0.0.service``. 
Replace ``spidev0.0`` with your desired userspace SPI device.
  
# License

- Licensed under the MIT license. See ``LICENSE`` for details.
