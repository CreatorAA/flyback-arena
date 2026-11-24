"""Build closed, rounded scenery islands for a moving game camera. No changes to referee terrain.

Run with the renderer's numpy/scipy/scikit-image environment, then pass the output directory
to export_backdrops.py --cache. Only the cache file in the specified directory is written.
"""
import argparse
import sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"flyback/client/tools"))
import upstream_art
import terrain
from prep import DECOR

def island(sp):
    small = sp["name"].startswith("s")
    rx, ry = sp["rx"], sp["ry"]
    seed = sp["seed"]
    # Build a signed, radially bounded contour. Adding noise to a zero-clamped
    # outside field produces land all the way to the rectangular sampling box.
    rng = np.random.default_rng(seed)
    res = .07 if not small else .045
    margin = 1.0
    xmax, ymax = 1.48*rx + margin, 1.48*ry + margin
    xs = -xmax + (np.arange(int(np.ceil(2*xmax/res)))+.5)*res
    ys = -ymax + (np.arange(int(np.ceil(2*ymax/res)))+.5)*res
    x,y = np.meshgrid(xs/rx,ys/ry)
    a=np.arctan2(y,x); radius=np.ones_like(a)
    for k,w in [(2,.055),(3,.106),(5,.073),(7,.032),(10,.015)]:
        radius+=w*np.cos(k*a+rng.uniform(0,2*np.pi))
    distance=(radius-np.hypot(x,y))*min(rx,ry)
    distance+=terrain.coarse_noise(x.shape,max(3,int(.85/res)),seed+5)*(.08 if not small else .02)
    mask=distance>0
    V,F,S=terrain.build_island(mask,(-xmax,-ymax),res=res,seed=seed,
        corner_r=.18 if not small else .075, edge_wobble=.018,
        depth_max=7.8 if not small else 2.8,
        taper_k=.38 if not small else .55, taper_p=1.33,
        cliff_h=1.00 if not small else .27, col_spacing=.7 if not small else .28,
        col_amp=.19 if not small else .06, ledge=.095 if not small else .035,
        rough_amp=.25 if not small else .07, mc_step=1)
    # A silhouette can look fine from the opening angle while missing a whole
    # cliff face in the final camera. Check the pre-decimation closed surface.
    edges=np.sort(F[:,[[0,1],[1,2],[2,0]]].reshape(-1,2),axis=1)
    _,counts=np.unique(edges,axis=0,return_counts=True)
    if not len(counts) or np.any(counts!=2):
        raise ValueError(f"{sp['name']}: open/nonmanifold scenery")
    return V,F,S

if __name__=="__main__":
    ap=argparse.ArgumentParser();ap.add_argument("output",type=Path)
    target=ap.parse_args().output
    layers={}
    for sp in DECOR:
        name=sp["name"];v,f,s=island(sp)
        for k,val in [("V",v),("F",f),("S",s)]:layers[name+"__"+k]=val
        print(name,len(v),len(f),"closed",flush=True)
    target.mkdir(parents=True,exist_ok=True)
    np.savez_compressed(target/"decor.npz",**layers)
