#!/usr/bin/python3

import usb.core
import usb.util
import struct as st
import collections

from contextlib import contextmanager


class Device:
    def __init__(self, product_name=None, idVendor=None, idProduct=None):
        devs = usb.core.find(find_all=True)
        devs = [d for d in devs]
        if idVendor:
            devs = [d for d in devs if d.idVendor == idVendor]
        if idProduct:
            devs = [d for d in devs if d.idProduct == idProduct]
        if product_name:
            devs = [d for d in devs if usb.util.get_string(d, d.iProduct) == product_name]

        if len(devs) == 0:
            raise ValueError('Device not found')
        self.dev = devs[0]
        # self.dev.set_configuration()
        
        self.general_iface = self.interface("general interface")
        self.cfg_out, self.cfg_in = self.general_iface.endpoints()
        self.dev.set_interface_altsetting(self.general_iface, 0)
        self.configs = self.fetch_configs()

    def interface(self, name, matcher= lambda x:True):
        cfg = self.dev.get_active_configuration()
        for iface in cfg:
            if matcher(iface) and usb.util.get_string(self.dev, iface.iInterface) == name:
                return iface
        
    @contextmanager
    def alt_setting_ctx(self, iface, altsetting):
        self.dev.set_interface_altsetting(iface, altsetting)
        try:
            yield iface
        finally:
            self.dev.set_interface_altsetting(iface, 0)

    def cfg_xfer(self, tx):
        self.cfg_out.write(tx)
        i = self.cfg_in.read(256)
        l, = st.unpack_from("H", i, 0)
        rx = i[2:]
        while len(rx) < l:
            rx += self.cfg_in.read(256)
            
        return rx

    def fetch_configs(self):
        cfg = collections.OrderedDict()
        num_configs, = st.unpack("H", self.cfg_xfer(st.pack("B", 0)))
        for i in range(num_configs):
            size,  = st.unpack("H", self.cfg_xfer(st.pack("=BH", 1, i)))
            name   = self.cfg_xfer(st.pack("=BH", 2, i)).tobytes().decode("utf-8")
            format = self.cfg_xfer(st.pack("=BH", 3, i)).tobytes().decode("utf-8")
            if size != st.calcsize(format):
                print(f"warning: format ({format}) for {name} has size of {st.calcsize(format)} whereas the remote memory is of size {size}")
            cfg[name] = (i, size, format,)
        return cfg

    def get_config(self, target):
        if target not in self.configs:
            print(target + " not in " + str(self.configs))
        idx, _, fmt = self.configs[target]
        return st.unpack(fmt, self.cfg_xfer(st.pack("=BH", 4, idx)))

    def set_config(self, target, values=(), verbose=False):
        if target not in self.configs:
            print(target + " not in " + str(self.configs))

        idx, _, fmt = self.configs[target]
        packed = st.pack(fmt, *values)
        self.cfg_xfer(st.pack("=BH", 5, idx) + packed)
        if verbose:
            print(f"set {target} to {values}")

