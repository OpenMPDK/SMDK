Building QEMU
-------------

    $ cd ..
    $ ./build_lib.sh qemu


Preparing QEMU disk image
-------------------------

Update the path of the Ubuntu iso file and create the QEMU disk image using the following commands.

    $ vi create_gui_image.sh     # Update "UBUNTU_ISO" path.
    $ ./create_gui_image.sh

QEMU will be started automatically after disk image is created. Connect to QEMU through VNC Viewer and install Ubuntu OS.

After Ubuntu installation is finished, you can run QEMU using the script below. Follow the guides in the script to setup ssh connection.

    $ ./setup_gui_ssh.sh


Run QEMU with CXL memory
------------------------

You can emulate CXL memory with the following script.

    $ ./run_cxl_emu_gui.sh       # Default setting: 6 cores, 8GB DRAM, 1GB CXL Memory. 


Connect to QEMU
---------------

You can use the following scripts to connect to QEMU through QEMU monitor (port: 45454) and sshd (port: 2242).

    # Connect through QEMU Monitor
    $ ./connect_monitor.sh

    # Connect through sshd
    $ ./connect_ssh.sh
