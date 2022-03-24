Building QEMU
-------------

    $ cd ..
    $ ./build_lib.sh qemu


Building Kernel
---------------

The Kernel build must be preceded to execute the QEMU VM.

    $ cd ..
    $ ./build_lib.sh kernel


Preparing rootfs image
----------------------

Download the Kernel image, or create it using the following command:

    $ ./create_rootfs.sh


Setup VM network
------------------

For communication between VMs, a TUN/TAP virtual interface must be added to br0.
In advance, you must enable br0:

 * QEMU network enabling: https://wiki.itplatform.samsungds.net:8090/x/bBuuB

You can use the following scripts to automatically add the TUN / TAP interface to use by the VMs.

    $ ./setup_bridge -c <number of VMs>
    $ brctl show
    bridge name     bridge id               STP enabled     interfaces
    br0             8000.00d8619ff96f       no              eno2
                                                            tap0
                                                            tap1
                                                            tap2

