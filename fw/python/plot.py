#!/bin/python

import matplotlib.pyplot as plt
import numpy as np
import argparse

from device import Device

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Plot variables')
    parser.add_argument('--id_vendor', dest='id_vendor', type=int, default=0xffff, help='usb vendor id of the target device')
    parser.add_argument('--id_product', dest='id_product', type=int, default=0x1234, help='usb product id of the target device')
    parser.add_argument('--eval', dest='eval', type=str, default=None, help='code to mogrify the values')
    parser.add_argument('--x', dest='x', type=int, default=64, help='count of the samples (horizontal width)')
    parser.add_argument('vals', metavar='Vals', default=["servo.state"], type=str, nargs='*', help='the variables to plot')

    args = parser.parse_args()

    dev = Device(idVendor=args.id_vendor, idProduct=args.id_product)

    x = np.arange(args.x)

    # You probably won't need this if you're embedding things in a tkinter plot...
    plt.ion()

    fig = plt.figure()
    ax = fig.add_subplot(111)


    def fetch():
        data = []
        for i, conf in enumerate(args.vals):
            data += [d for d in dev.get_config(conf)]
        return data

    prev_data = fetch()
    def fetch_and_mogrify():
        global prev_data
        data = fetch()
        if args.eval:
            data_ = eval(args.eval)
        else:
            data_ = data
        prev_data = data
        return data_

    lines = []
    ys = []

    colors = 'rgbcmyk'
    for i, d in enumerate(fetch_and_mogrify()):
        y = np.zeros((args.x,))
        y[-1] = d
        style = colors[i%len(colors)] + '-'
        lines.append(ax.plot(x, y, style)[0])
        ys.append(y)

    plt_ax = plt.gca()
    while True:
        data = fetch_and_mogrify()
        for line, y, d in zip(lines, ys, data):
            y[:-1] = y[1:]
            y[-1] = d
            line.set_ydata(y)
            
        plt_ax.relim()
        plt_ax.autoscale_view()
        plt.pause(0.05)
        
