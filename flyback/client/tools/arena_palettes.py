"""Only fork-local bakes use these palettes. The archived renderer is untouched."""
import copy
import themes

LOOKS = {
    "tidelock": ("oldtown", ["7faf9e","9dc6b0","558f83","548d80","c8d19a","be955e","ddba79","96774f","42745a"],
                 ["#72aca2","#a1cdb7","#ba9a72","#9b8579","#685e74","#376577","#82c2ce"], "lockwheel", [(0.55,"cypress"),(.45,"round")]),
    "kiln": ("canyon", ["ce8964","e3a575","b56d55","b0795e","ecc698","dfaf87","efd0a3","be8f74","626e52"],
             ["#c88a65","#e5b888","#c77955","#a7574d","#74545d","#5d5261","#e9b9a7"], "kilnhouse", [(.48,"juniper"),(.32,"spire"),(.20,"cactus")]),
    "windrose": ("sunset", ["c8ba71","e1cc7f","a39a60","9da465","e6d48b","d6b98a","ebd7a8","b09b72","7a9164"],
                 ["#b8bb72","#e5d08a","#cbb584","#99877a","#817786","#746d82","#d4d5b5"], "windmill", [(.50,"cypress"),(.50,"round")]),
    "moonwell": ("ruins", ["a5a7c6","c9c8df","8186a9","8c99b0","cecfce","c2c4cf","dbdbe3","a0a2b2","7682a4"],
                 ["#a7abc5","#d1d0de","#adabc4","#908da8","#646985","#535a7b","#b6c0d8"], "orrery", [(.58,"cypress"),(.42,"heather")]),
}

def register():
    for name,(base,ground,rock,feature,trees) in LOOKS.items():
        th=copy.deepcopy(themes.THEMES[base])
        th.setdefault('ground',{}).update({k:tuple(bytes.fromhex(v)) for k,v in zip(
            ('mid','light','dark','lush','rim','dirt','dirt_hi','dirt_lo','tall'),ground)})
        th.setdefault('pal',{}).update(dict(zip(('grass','grass_rim','soil','rock_a','rock_b','rock_deep','haze'),rock)))
        th['pal']['tree_pals'] = [(0.55,(rock[0],rock[0],rock[1])),(0.45,(rock[4],rock[0],rock[1]))]
        th['pal'].update(tall_base='#'+ground[-1],tall_mid='#'+ground[3],tall_tip='#'+ground[4])
        th['decor'].update(features=(feature,feature,feature,feature),mix=trees,density=.62,waterfall=(name=='tidelock'))
        if name=='tidelock':th['gstyle']={'planks':1.4,'wood':[(143,106,70),(183,149,106),(215,182,132)]}
        if name=='moonwell':th['gstyle']={'pave':'all','ring':True,'ticks':'none'}
        th.get('gstyle',{}).pop('planks',None)  # authored deck spans below; never tile a plank across a void
        themes.THEMES[name]=th
def finish_surface(im,key,m,ppm):
    """Small, level markings stay on the authority's open floor and cast no shadows."""
    if key not in LOOKS:return im
    from PIL import Image,ImageDraw
    import math,random
    rng=random.Random(13)
    dr=ImageDraw.Draw(im,"RGBA");w,h=m["W"]*.5,m["H"]*.5;cx,cy=w/2,h/2
    def pts(a):return [(round((x+.6)*ppm),round((y+.6)*ppm)) for x,y in a]
    def line(a,color,mm):dr.line(pts(a),fill=color,width=max(1,round(mm*ppm)),joint="curve")
    def poly(a,color):dr.polygon(pts(a),fill=color)
    def rect(x0,y0,x1,y1,c):dr.rectangle(pts([(x0,y0),(x1,y1)]),fill=c)
    def ring(cx,cy,r,c,width):
        line([(cx+r*math.cos(k*math.tau/160),cy+r*math.sin(k*math.tau/160)) for k in range(161)],c,width)
    if key=="tidelock":
        for xmid in [6.5,w-6.5]:
            for yc,half in [(cy,1.),(2.1,.42),(h-2.1,.42)]:
                x0,x1=xmid-1.11,xmid+1.11;y0,y1=yc-half,yc+half
                for j in range(10):
                    x=x0+(x1-x0)*j/10;k=rng.randrange(-13,13)
                    rect(x,y0,x+.216,y1,(174+k,136+k,97+k,242))
                    line([(x+.017,y0+.017),(x+.017,y1-.017)],(236,205,156,190),.016)
                    for dy in [y0+.095,y1-.095]:rect(x+.077,dy,x+.105,dy+.024,(85,77,66,185))
                    yy=rng.uniform(y0+.25,y1-.15);line([(x+.075,yy),(x+.057,yy+.06),(x+.062,min(yy+.25,y1-.08))],(105,75,53,112),.012)
                for y in [y0,y1-.054]:rect(x0,y,x1,y+.053,(101,83,63,235))
    elif key=="moonwell":
        for r in (2.30,2.38,3.05):
            ring(cx,cy,r,(224,218,178,180) if r<3 else (111,117,155,160),.037)
        for i in range(32):
            a=i*math.tau/32;d=(math.cos(a),math.sin(a))
            l=.20 if i%4==0 else .09
            line([(cx+(2.4+l)*d[0],cy+(2.4+l)*d[1]),(cx+2.4*d[0],cy+2.4*d[1])],(214,202,158,192),.045)
    elif key=="windrose":
        ring(cx,cy,.86,(240,216,156,175),.052)
        for i in range(8):
            a=-math.pi/2+math.tau*i/8; d=(math.cos(a),math.sin(a));n=(-d[1],d[0]);l=.81 if i%2==0 else .50
            tip=(cx+l*d[0],cy+l*d[1]);root=(cx,cy)
            poly([root,(cx+.14*n[0],cy+.14*n[1]),tip],(88,113,104,180))
            poly([root,(cx-.14*n[0],cy-.14*n[1]),tip],(245,222,163,220))
    else:
        for i in range(4):
            a=i*math.tau/4+.25;d=(math.cos(a),math.sin(a));n=(-d[1],d[0])
            for j in range(4):
                r=1.40+j*.18
                line([(cx+d[0]*r-.16*n[0],cy+d[1]*r-.16*n[1]),(cx+d[0]*r+.16*n[0],cy+d[1]*r+.16*n[1])],(234,199,152,138),.044)
    return im
