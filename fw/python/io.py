#!/usr/bin/python3

from device import Device
from array import array
import struct as st
import collections
import sys
import time 

import yaml
import argparse


def argsToMsg(args):
    if len(args) == 0:
        return bytearray()
    else:
        return st.pack('=i', args[0]) + argsToMsg(args[1:])


def boolify(s):
    if s == 'True':
        return True
    if s == 'False':
        return False
    raise ValueError("")

def byteify(s):
    return s.encode()

def customInt(s):
    if len(s) > 2:
        if s[0:2] == "0x":
            return int(s[2:], 16)
        elif s[0:2] == "0b":
            return int(s[2:], 2)
    raise ValueError("")


def autoconvert(s):
    for fn in (boolify, customInt, int, float, byteify):
        try:
            return fn(s)
        except:
            pass
    return s


def flush(dev):
    try:
        while True:
            msg = dev.read(self.in_ep, 512)
            print("flushed" + str(msg))
            if len(msg) == 0:
                break
    except:
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='get and put configuration from/to the device')
    parser.add_argument('--id_vendor', dest='id_vendor', type=str,
                        default=0xffff, help='usb vendor id of the target device')
    parser.add_argument('--id_product', dest='id_product', type=str,
                        default=None, help='usb product id of the target device')
    parser.add_argument('--name', dest='product_name', type=str,
                        default="my_psu", help='product name of the usb device')
    parser.add_argument('--format', dest='format', type=str,
                        help='optional output format')

    parser.add_argument('action', nargs='?', choices=['describe', 'get', 'set', 'dump', 'watch'],
                        default='describe', help='what to do')
    parser.add_argument('target', nargs='?',
                        default="", help='the target configuration to reference')
    parser.add_argument('vals', metavar='Vals', default=[],
                        type=str, nargs='*', help='the variables to send')

    args = parser.parse_args()
    dev = Device(product_name=args.product_name,
                 idVendor=autoconvert(args.id_vendor),
                 idProduct=autoconvert(args.id_product))

    if args.action == 'describe':
        for k, v in sorted(dev.configs.items()):
            print("{:0>3d}: {:s}: {:s}, {:d}".format(v[0], k, v[2], v[1]))
    if args.action == 'dump':
        d = {k: dev.get_config(k) for k, info in sorted(
            dev.configs.items()) if info[1] != 0}
        print(yaml.safe_dump(d))

    if args.action == 'set':
        dev.set_config(args.target, tuple(autoconvert(s)
                                          for s in args.vals), True)

    if args.action == 'get':
        c = dev.get_config(args.target)
        if args.format is not None:
            print(args.format.format(*c))
        else:
            print(" ".join((str(i) for i in c)))

    if args.action == 'watch':
        while True:
            c = dev.get_config(args.target)
            if args.format is not None:
                print(args.format.format(*c))
            else:
                print(" ".join((str(i) for i in c)))
