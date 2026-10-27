#!/usr/bin/env python3
# Drive the plain original (no oracle) under Xvfb: run terranx.exe, then follow a step file:
#   wait SECONDS | key KEYSYM [n] | text STRING | shot NAME | click X Y [S|C] | rclick X Y [S|C] | move X Y
#   | press X Y | release (the left button held / let go: drags) | quit
# Screenshots go to OUTDIR/NAME.png. Usage: orig_drive.py STEPFILE OUTDIR [extra ini lines file]
# Keys are X keysyms with C+/S+/A+ modifiers (sent with XTEST to the game's Xvfb display). The game
# runs in work/popup_orig_run with harness/oracle/runtime's ini (no sound, no DirectDraw, no movie).
import sys, os, time, subprocess, shutil, random, glob, signal
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))
steps=[l.strip() for l in open(sys.argv[1]) if l.strip() and not l.startswith(';')]
out=os.path.abspath(sys.argv[2]); os.makedirs(out,exist_ok=True)
run=os.path.join(ROOT,'work/popup_orig_run'); shutil.rmtree(run,ignore_errors=True); os.makedirs(run)
for f in glob.glob(ROOT+'/game/*'):
    os.symlink(f,os.path.join(run,os.path.basename(f)))
os.remove(os.path.join(run,'saves')) if os.path.islink(os.path.join(run,'saves')) else None
os.makedirs(os.path.join(run,'saves'),exist_ok=True)
ini=open(ROOT+'/harness/oracle/runtime/Alpha Centauri.Ini').read()
if len(sys.argv)>3: ini+=open(sys.argv[3]).read()
p=os.path.join(run,'Alpha Centauri.Ini')
if os.path.lexists(p): os.remove(p)
open(p,'w').write(ini)
disp=':%d'%random.randint(900,990)
X=subprocess.Popen(['Xvfb',disp,'-screen','0','1024x768x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1.5)
# The prefix with the game's fonts and Windows' text measurement (harness/wine_env.sh).
wenv=subprocess.run(['bash','-c','ROOT="$1"; . "$1/harness/wine_env.sh"; env -0','-',ROOT],capture_output=True).stdout
env=dict(os.environ,**dict(kv.split('=',1) for kv in wenv.decode().split('\0') if kv.startswith(('WINE','FREETYPE'))))
env['DISPLAY']=disp
W=subprocess.Popen(['wine','terranx.exe'],cwd=run,env=env,stdout=open(run+'/wine.log','w'),stderr=subprocess.STDOUT)
os.environ['DISPLAY']=disp
from Xlib import display, X as XX, XK
from Xlib.ext import xtest
import mss, mss.tools
d=display.Display(disp)
def key(name):
    mods=[]
    if '+' in name and len(name)>1:
        parts=name.split('+'); mods=parts[:-1]; name=parts[-1]
    kc=d.keysym_to_keycode(XK.string_to_keysym(name))
    mk=[d.keysym_to_keycode(XK.string_to_keysym({'S':'Shift_L','C':'Control_L','A':'Alt_L'}[m])) for m in mods]
    for m in mk: xtest.fake_input(d,XX.KeyPress,m)
    xtest.fake_input(d,XX.KeyPress,kc); d.sync(); time.sleep(0.05)
    xtest.fake_input(d,XX.KeyRelease,kc)
    for m in mk: xtest.fake_input(d,XX.KeyRelease,m)
    d.sync(); time.sleep(0.15)
try:
  with mss.MSS() if hasattr(mss,'MSS') else mss.mss() as s:
    for st in steps:
        a=st.split(None,1); c=a[0]; arg=a[1] if len(a)>1 else ''
        if W.poll() is not None: print('game exited'); break
        if c=='wait': time.sleep(float(arg))
        elif c=='key':
            b=arg.split(); n=int(b[1]) if len(b)>1 else 1
            for _ in range(n): key(b[0])
        elif c=='text':
            for ch in arg: key({' ':'space','.':'period','-':'minus'}.get(ch,ch))
        elif c=='shot':
            img=s.grab(s.monitors[0]); mss.tools.to_png(img.rgb,img.size,output=f'{out}/{arg}.png'); print('shot',arg)
        elif c=='click':  # a left click, shift (S) / ctrl (C) held when given
            b=arg.split(); x,y=int(b[0]),int(b[1])
            mk=[d.keysym_to_keycode(XK.string_to_keysym({'S':'Shift_L','C':'Control_L'}[m])) for m in b[2:]]
            xtest.fake_input(d,XX.MotionNotify,x=x,y=y); d.sync(); time.sleep(0.1)
            for m in mk: xtest.fake_input(d,XX.KeyPress,m)
            d.sync(); time.sleep(0.05)
            xtest.fake_input(d,XX.ButtonPress,1); d.sync(); time.sleep(0.05); xtest.fake_input(d,XX.ButtonRelease,1); d.sync()
            for m in mk: xtest.fake_input(d,XX.KeyRelease,m)
            d.sync(); time.sleep(0.2)
        elif c=='rclick':  # a right click, shift (S) / ctrl (C) held when given
            b=arg.split(); x,y=int(b[0]),int(b[1])
            mk=[d.keysym_to_keycode(XK.string_to_keysym({'S':'Shift_L','C':'Control_L'}[m])) for m in b[2:]]
            xtest.fake_input(d,XX.MotionNotify,x=x,y=y); d.sync(); time.sleep(0.1)
            for m in mk: xtest.fake_input(d,XX.KeyPress,m)
            xtest.fake_input(d,XX.ButtonPress,3); d.sync(); time.sleep(0.05); xtest.fake_input(d,XX.ButtonRelease,3); d.sync()
            for m in mk: xtest.fake_input(d,XX.KeyRelease,m)
            d.sync(); time.sleep(0.2)
        elif c=='move':
            x,y=map(int,arg.split()); xtest.fake_input(d,XX.MotionNotify,x=x,y=y); d.sync()
        elif c=='press':
            x,y=map(int,arg.split()); xtest.fake_input(d,XX.MotionNotify,x=x,y=y); d.sync(); time.sleep(0.1)
            xtest.fake_input(d,XX.ButtonPress,1); d.sync()
        elif c=='release':
            xtest.fake_input(d,XX.ButtonRelease,1); d.sync(); time.sleep(0.2)
        elif c=='quit': break
finally:
    # Only the processes started here are stopped (never wineserver -k: the prefix is shared).
    try:
        W.send_signal(signal.SIGTERM); W.wait(10)
    except Exception:
        W.kill()
    X.terminate()
