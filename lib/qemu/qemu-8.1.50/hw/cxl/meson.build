system_ss.add(when: 'CONFIG_CXL',
               if_true: files(
                   'cxl-component-utils.c',
                   'cxl-device-utils.c',
                   'cxl-mailbox-utils.c',
                   'cxl-host.c',
                   'cxl-cdat.c',
                   'cxl-events.c',
                   'cxl-cpmu.c',
                   'switch-mailbox-cci.c',
               ),
               if_false: files(
                   'cxl-host-stubs.c',
               ))
system_ss.add(when: 'CONFIG_I2C_MCTP_CXL', if_true: files('i2c_mctp_cxl.c'))

system_ss.add(when: 'CONFIG_ALL', if_true: files('cxl-host-stubs.c'))

subdir('vendor')
