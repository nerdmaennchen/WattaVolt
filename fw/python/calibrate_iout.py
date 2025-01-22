from device import Device
from time import sleep
from contextlib import contextmanager
import matplotlib.pyplot as plt
import numpy as np
import argparse

@contextmanager
def auto_off(dev, ch):
    try:
        yield dev
    finally:
        dev.set_config(f"vout{ch}", (0,))
        dev.set_config(f"iout{ch}", (0,))
        dev.set_config(f"output{ch}.enable", (0,))

def linreg(xx, yy, order=2):
    X = np.hstack([xx**o for o in range(order)])
    betas = np.linalg.inv(X.transpose() @ X) @ X.transpose() @ yy
    # print(f"{betas=}")
    error = np.sqrt(np.sum(((X @ betas) - yy) ** 2))
    # print(f"backprojection error: {error=}")
    return betas, error


def gather_calib_data(dev, ch, start_i, stop_i, steps, oversample):
    with auto_off(dev, ch):
        dev.set_config(f"output{ch}.enable", (1,))
        dev.set_config(f"vout{ch}", (3,))
        x = np.linspace(start_i, stop_i, steps)
        readings = np.zeros((0, 4))
        readings_g = {}
        xx = np.zeros((0, 1))
        raw_pwm = np.zeros((0, 1))
        for v in x:
            print(f"setting iout to: {v}")
            dev.set_config(f"iout{ch}", (v,))
            dev.set_config(f"vout{ch}", (v + 1,))
            sleep(.25)
            pwm, = dev.get_config(f"iout{ch}.raw")
            raw_pwm = np.vstack((raw_pwm, np.array(((pwm,),))))

            readings_ = np.zeros((0, 4))
            for _ in range(oversample):
                v0, i0, v1, i1 = dev.get_config("adc")
                readings = np.vstack((readings, np.array(((v0, i0, v1, i1),))))
                readings_ = np.vstack((readings_, np.array(((v0, i0, v1, i1),))))
                xx  = np.vstack((xx, np.array(((v,),))))
                sleep(.01)
            readings_g[v * 100] = readings_
            print(f"{v0=}, {i0=}, {v1=}, {i1=}")

    col = 2*ch+1
    xx = xx.reshape((-1,1))
    yy = readings[:, col].reshape((-1, 1))
    return xx, yy

def filter(betas, xx, yy, take):
    X = np.hstack([xx**o for o in range(2)])
    errors = ((X @ betas) - yy) ** 2

    s = np.argsort(errors, axis=0)
    best_n = s[:-round(s.shape[0] * (1-take)), 0]
    print(f"{best_n.shape=}")
    xx1 = xx[best_n]
    yy1 = yy[best_n]

    print(f"{xx.shape=} {xx1.shape=}")
    betas1, error1 = linreg(xx1, yy1)
    return betas1, xx1, yy1

def eval(xx, yy, filter_steps, take_best):
    betas, error = linreg(xx, yy)

    plt.plot(xx, yy, "x")

    for i in range(filter_steps):
       betas, xx, yy = filter(betas, xx, yy, take_best)
    
    a = -betas[0,0] / betas[1,0]
    b = 1 / betas[1,0]
    print(f"{a} {b}")

    X = np.hstack([xx**o for o in range(2)])
    errors = ((X @ betas) - yy) ** 2
    print(f"errors: {np.mean(errors)}")
    plt.plot(xx, X @ betas)
    plt.show()
    return a, b

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='get and put configuration from/to the device')
    
    parser.add_argument('--start', type=float, help='start current', default=0.)
    parser.add_argument('--stop',  type=float, help='stop current', default=2.5)
    parser.add_argument('--steps', type=int, help='how many steps between start and stop', default=50)

    parser.add_argument('--oversample', type=int, help='how many samples to take per setting', default=5)
    parser.add_argument('--sample_file', type=str, help='instead of taking samples from the output, use a file with calibration data', default=None)
    
    parser.add_argument('--filter_steps', type=int, help='in evaluation: how many filter steps to perform', default=3)
    parser.add_argument('--take_best', type=float, help='in evaluation: how many filter steps to perform', default=.8)
    
    parser.add_argument('channel', nargs=1, choices=[0, 1], type=int, help='the channel to calibrate')

    args = parser.parse_args()

    dev = Device(product_name="my_psu", idVendor=0xffff, idProduct=0x1234)
    ch = args.channel[0]
    old_cfg = dev.get_config(f"corr.iout{ch}")
    print(f"the old config: {old_cfg}")

    if args.sample_file == None:
        dev.set_config(f"corr.iout{ch}", (0, 1))
        xx, yy = gather_calib_data(dev, ch, args.start, args.stop, args.steps, args.oversample)
        np.savez(f"calib_samples_{ch}.npz", xx=xx, yy=yy)
    else:
        f = np.load(args.sample_file)
        xx, yy = f["xx"], f["yy"]

    a, b = eval(xx, yy, args.filter_steps, args.take_best)
    dev.set_config(f"corr.iout{ch}", (a, b))

