"""Silhouette landmarks for the four new courses, rooted on the non-playable islands.
The closed curves and solid fittings are actual 3D mesh; no camera-facing images.
"""
import math
import numpy as np
import props as P
from props import C
tau=math.tau

def block(m,x,y,z,a,b,h,color):
    P.box(m,x-a/2,x+a/2,y-b/2,y+b/2,z,z+h,C(color))
def rod(m,a,b,r,col):
    P.tube(m,[a,b],r,C(col),seg=7)
def wheel(m,c,u,v,r,th,col,n=24):
    c=np.asarray(c);u=np.asarray(u);v=np.asarray(v)
    pts=[c+r*(u*math.cos(i*tau/n)+v*math.sin(i*tau/n)) for i in range(n+1)]
    P.tube(m,pts,th,C(col),seg=6)

def lockwheel(x,y,z,s,rng,T,L,M):
    oak="#98683F";brass="#E4B962";iron="#47716D"
    for dx in (-.78,.78):
        block(M,x+dx,y,z,.32,.58,1.85,oak)
        block(M,x+dx,y,z+.15,.37,.61,.23,iron)
    block(M,x,y,z+1.66,2.05,.46,.31,iron)
    for i in range(4):block(M,x,y,z+.24+i*.31,1.27,.23,.262,("#BC9162","#AB7A4E")[i%2])
    c=np.array((x,y-.35,z+1.50));u=np.array((1,0,0));v=np.array((0,0,1))
    wheel(M,c,u,v,.60,.074,brass)
    for i in range(6):
        q=c+.63*(u*math.cos(i*tau/6)+v*math.sin(i*tau/6));rod(M,c,q,.04,brass)
    rod(M,(x,y-.52,z+1.5),(x,y+.27,z+1.5),.13,iron)

def kilnhouse(x,y,z,s,rng,T,L,M):
    levels=[(.98,z),(.99,z+.15),(.92,z+.37),(.82,z+1.02),(.59,z+1.46),(.3,z+1.57),(.3,z+1.99),(.37,z+2.03),(.37,z+2.13)]
    P.stack(M,x,y,levels,list(map(C,["#86564D","#BC7556","#C88B68","#C67C54","#AC6450","#835953","#C99977","#976459"])),n=14,cap=C("#58484C"))
    for k in range(6):
        a=k*math.pi/6; p=(x+.53*math.cos(a),y-.79,z+.37+.5*math.sin(a));q=(x+.53*math.cos(a+math.pi/6),y-.79,z+.37+.5*math.sin(a+math.pi/6))
        rod(M,p,q,.12,"#E5B088")
    block(M,x,y-.87,z+.05,.76,.16,.54,"#513C3B")
    block(M,x,y-.97,z+.13,.54,.08,.115,"#C97D49")
    for i in range(3):
        for j in range(3-i): block(M,x+1.0+(j-(2-i)*.5)*.31,y-.12,z+.22*i,.29,.5,.20,"#BB805B")

def windmill(x,y,z,s,rng,T,L,M):
    P.stack(M,x,y,[(.78,z),(.80,z+.18),(.61,z+2.52),(.87,z+2.61),(.07,z+3.32)],
            [C(c) for c in ("#9A7955","#E7CCA3","#665B4F","#587F7A")],n=10,cap=C("#AB9664"))
    block(M,x,y-.63,z+.15,.34,.15,.7,"#785F4B")
    c=np.array([x,y-.77,z+2.14]); hub="#AB8253"
    for i in range(4):
        a=(i+.34)*tau/4
        d=np.array([math.cos(a),0,math.sin(a)]);side=np.array([-d[2],0,d[0]])
        rod(M,c,c+d*1.38,.063,hub)
        poly=[c+d*.51+side*.02,c+d*1.34+side*.02,c+d*1.30+side*.40,c+d*.56+side*.26]
        for k in range(2):
            P.quad(M,poly if k==0 else list(reversed(poly)),C("#EEDFB4"))
        for t in [.66,.92,1.17]:rod(M,c+d*t,c+d*t+side*(.24+.12*t),.021,"#BAA272")
    rod(M,c+np.array([0,-.17,0]),c+np.array([0,.34,0]),.16,"#6D7972")

def orrery(x,y,z,s,rng,T,L,M):
    P.stack(M,x,y,[(1.1,z),(1.1,z+.12),(.88,z+.19),(.77,z+.35),(.46,z+.40),(.29,z+.9),(.44,z+.99)],
            [C(c) for c in ("#8D94A7","#C9CADF","#838DA9","#C3BDCC","#838CA2","#CBB992")],n=12,cap=C("#ECE3BC"))
    c=np.array([x,y,z+1.83]);v=(0,0,1);u=(1,0,0)
    wheel(M,c,u,v,.99,.071,"#D6B66C")
    wheel(M,c,(.81,.58,0),(-.25,.36,.9),.83,.039,"#B4C1D0")
    wheel(M,c,u,(0,.93,.37),.91,.048,"#DDC592")
    rod(M,(x,y,z+.94),(x,y,z+2.8),.036,"#BBAB7E")
    P.blob(M,x,y,z+1.83,.28,.28,.28,C("#6C8DAB"),C("#D6E2DF"))
    for i in range(6):
        a=tau*i/6;P.blob(M,x+.99*math.cos(a),y,z+1.83+.99*math.sin(a),.09,.09,.09,C("#B89D68"),C("#E9D8A1"),sub=1)
def register():
    P.FEATURES.update(lockwheel=lockwheel,kilnhouse=kilnhouse,windmill=windmill,orrery=orrery)
