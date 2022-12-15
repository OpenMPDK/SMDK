Building QEMU
-------------

    $ cd ..
    $ ./build_lib.sh qemu


Preparing rootfs image
----------------------

Download the Ubuntu iso file, and create QEMU image using the following command:

    $ ./create_gui_image.sh


Setup VM network
------------------

For communication between VMs, a TUN/TAP virtual interface must be added to br0.
In advance, you should enable br0.
You can use the following scripts to automatically add the TUN / TAP interface to use by the VMs.

    $ ./setup_bridge -c <number of VMs>
    $ brctl show
    bridge name     bridge id               STP enabled     interfaces
    br0             8000.00d8619ff96f       no              eno2
                                                            tap0
                                                            tap1
                                                            tap2
