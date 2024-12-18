omroneip
=======

An [EPICS](http://www.aps.anl.gov/epics) [asyn](https://github.com/epics-modules/asyn) driver for communicating with OmronNJ PLCs using etherIP. This driver is still under development, but once complete will support the _large forward open_ CIP specification as well as _packing_ of CIP responses. The _large forward open_ message specification uses connected messaging and supports single request/response packet sizes of up to 1994 bytes. This driver is being developed as an alternative to the current EPICS [etherIP](https://github.com/epics-modules/ether_ip) module for the OmronNJ implementation of CIP. It should be compatible with CIP running on Alan-Bradley PLCs, but this has not been tested on these PLCs which have a slightly different CIP implementation. This driver uses the open source CIP communications library [libplctag](https://github.com/libplctag/libplctag).

Credits
---------------------

Source code being developed by [Observatory Sciences Ltd.](https://www.observatorysciences.co.uk) for [STFC](https://www.ukri.org/councils/stfc/).

Supported platforms
-------------------

This driver is being built and tested on Centos 9, using EPICS base 3.15.9 and asyn R4-44-2. It is being tested with an OmronNJ 101-9000 PLC.

Software manual
-------------------
See [/docs/Omroneip_Software_Manual.md](https://github.com/Observatory-Sciences/omroneip/blob/main/docs/Omroneip_Software_Manual_v3.md) for the latest documentation

Doxygen documentation
-------------------
https://observatory-sciences.github.io/omroneip/index.html
