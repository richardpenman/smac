#!/usr/bin/env python3
# xinput.py OUTDIR STEP... : input to the X display $DISPLAY (XTEST), for tests/screen/run.sh's
# interactive run. Steps: "key KEYSYM" (with A+ / C+ / S+ modifiers held), "click X Y" (the left
# button at display pixels X, Y), "wait SECONDS", "shot NAME" (the display as OUTDIR/NAME.png).
# Needs python-xlib and mss (tools/venv).
import os
import sys
import time

import mss
import mss.tools
from Xlib import X, XK, display
from Xlib.ext import xtest

d = display.Display(os.environ['DISPLAY'])
out = sys.argv[1]


def code(name):
    return d.keysym_to_keycode(XK.string_to_keysym(name))


def key(name):
    mods = []
    if '+' in name and len(name) > 1:
        *mods, name = name.split('+')
    held = [code({'S': 'Shift_L', 'C': 'Control_L', 'A': 'Alt_L'}[m]) for m in mods]
    for m in held:
        xtest.fake_input(d, X.KeyPress, m)
        d.sync()
        time.sleep(0.05)
    xtest.fake_input(d, X.KeyPress, code(name))
    d.sync()
    time.sleep(0.05)
    xtest.fake_input(d, X.KeyRelease, code(name))
    d.sync()
    time.sleep(0.05)
    for m in held:
        xtest.fake_input(d, X.KeyRelease, m)
    d.sync()
    time.sleep(0.3)


for step in sys.argv[2:]:
    a = step.split()
    if a[0] == 'key':
        key(a[1])
    elif a[0] == 'wait':
        time.sleep(float(a[1]))
    elif a[0] == 'click':
        xtest.fake_input(d, X.MotionNotify, x=int(a[1]), y=int(a[2]))
        d.sync()
        time.sleep(0.2)
        xtest.fake_input(d, X.ButtonPress, 1)
        d.sync()
        time.sleep(0.08)
        xtest.fake_input(d, X.ButtonRelease, 1)
        d.sync()
        time.sleep(0.5)
    elif a[0] == 'shot':
        with (mss.MSS() if hasattr(mss, 'MSS') else mss.mss()) as s:
            img = s.grab(s.monitors[0])
            mss.tools.to_png(img.rgb, img.size, output=os.path.join(out, a[1] + '.png'))
    print('xinput', step, flush=True)
