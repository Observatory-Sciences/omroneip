omroneip
=======

An [EPICS](http://www.aps.anl.gov/epics) [asyn](https://github.com/epics-modules/asyn) driver for communicating with OmronNJ PLCs using etherIP. This driver will support the _large forward open_ CIP specification as well as _packing_ of CIP responses. This driver is being developed as an alternative to the current EPICS [etherIP](https://github.com/epics-modules/ether_ip) module for Omrons implementation of CIP and may or may not be compatible with CIP running on Alan-Bradley PLCs.

Credits
---------------------

Source code being developed by [Observatory Sciences Ltd.](https://www.observatorysciences.co.uk) for [STFC](https://www.ukri.org/councils/stfc/).

Supported platforms
-------------------

This driver is being built and tested on Centos 9, using EPICS base 3.15.9 and asyn R4-44-2. It is being tested with an OmronJ 101-9000 PLC.
