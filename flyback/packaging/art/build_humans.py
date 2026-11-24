"""Four original, jointed playable figures for Flyback. Run in Blender 4.5+ (background).
All seams, headwear and equipment have actual volume. Vertex colours are sufficient at game scale.
The millimetre gameplay circle stays in dart.hpp, independent of hats, bags, antlers or tools.
"""
import sys, math, random
from pathlib import Path
from mathutils import Vector
import bpy, numpy as np
root=Path(__file__).resolve().parents[3];sys.path.insert(0,str(root/"flyback/client/tools"))
import upstream_art
from voxkit import VPart
from geom import MeshBuilder
from types import SimpleNamespace
UNIT=.032
OUT=root/"flyback/client/art/humans"; QA=root/"out/flyback-human-proof";OUT.mkdir(parents=True,exist_ok=True);QA.mkdir(parents=True,exist_ok=True)
INK="#34313D"; GOLD="#E4AE53"; BRASS="#AC7042"; BONE="#FFE5B5"; DEEP="#26313E"
def rounded(p,x0,x1,y0,y1,z0,z1,col):
    p.box(x0,x1,y0,y1,z0,z1,col).chamfer(x0,x1,y0,y1,z0,z1);return p
def plate(p,coords,front,back,col): # silhouette drawn in X/Z, extruded through Y
    # robust integer pixel-centre polygon fill (half-open parity)
    xmin,xmax=min(x for x,z in coords),max(x for x,z in coords);zmin,zmax=min(z for x,z in coords),max(z for x,z in coords)
    for x in range(math.floor(xmin),math.ceil(xmax)):
        for z in range(math.floor(zmin),math.ceil(zmax)):
            px,pz=x+.5,z+.5;inside=False
            for i,(ax,az) in enumerate(coords):
                bx,bz=coords[i-1]
                if (az>pz)!=(bz>pz) and px<(bx-ax)*(pz-az)/(bz-az)+ax:inside=not inside
            if inside:p.box(x,x+1,front,back,z,z+1,col)
    return p
def line(p,a,b,w,col):
    steps=max(abs(int(a[i]-b[i])) for i in range(3))*2+1
    for n in range(steps+1):
        v=[round(a[i]+(b[i]-a[i])*n/steps) for i in range(3)];p.box(v[0],v[0]+w,v[1],v[1]+w,v[2],v[2]+w,col)
def dots(p,coords,col):
    for x,y,z in coords:p.put(x,y,z,col)
def front(p,x0,x1,z0,z1,y,col):p.slab(x0,x1,y-.16,y+.06,z0,z1,col)
def back(p,x0,x1,z0,z1,y,col):p.slab(x0,x1,y-.06,y+.16,z0,z1,col)
def disc(p,cx,y,cz,r,metal,inside):
    for x in range(-r,r+1):
        for z in range(-r,r+1):
            d=x*x+z*z
            if d<=r*r+.4:p.box(cx+x,cx+x+1,y,y+1,cz+z,cz+z+1,metal if d>(r-.95)**2 else inside)
def headbase(p,skin,hair):
    rounded(p,-6,6,-5,5,17,25,skin);rounded(p,-5,5,-4,4,15,18,skin);rounded(p,-6,6,-5,5,24,26,hair)
    # Ears protrude one voxel; warm inside marks distinguish them from facial paint.
    p.boxm(6,7,-1,2,19,22,skin);p.slab(7,7.12,-.2,1.2,19.5,21.1,"#D58C74")
    p.slab(-7.12,-7,-.2,1.2,19.5,21.1,"#D58C74")
    p.slab(-.78,.78,-5.58,-4.88,18.1,19.5,skin)
def faces(tag,eye,lip):
    ret={}
    for fname in ["open","tight","happy"]:
        f=VPart("face_"+fname,(0,0,16))
        for sign in [-1,1]:
            x=sign*2.95
            if fname=="open":
                front(f,x-1.35,x+1.35,19.4,21.35,-5.1,"#FAE4C7");front(f,x-1.05,x+.95,19.55,21.82,-5.29,eye)
                front(f,x-.68,x+.50,20.18,21.55,-5.49,INK);front(f,x-.62,x+.03,20.94,21.60,-5.7,"#FFF6D7")
            elif fname=="tight":
                front(f,x-1.3,x+1.2,20,20.75,-5.16,eye);front(f,x+.8*sign-.36,x+.8*sign+.36,20.4,21.1,-5.19,eye)
            else:
                front(f,x-1.3,x+1.2,20.65,21.21,-5.2,eye);front(f,x-1.33,x-.78,20.12,20.65,-5.2,eye);front(f,x+.7,x+1.24,20.12,20.65,-5.2,eye)
            front(f,x-1.34,x+1.15,22.35,22.90,-5.10,eye)
            front(f,x-1.25,x+.6,18.75,19.18,-5.06,lip)
        front(f,-.82,1,17.05,17.48,-4.1,lip)
        ret[f.name]=f
    return ret
def figure(key):
    P={}
    def part(n,pivot):
        p=VPart(n,pivot);P[n]=p;return p
    body=part("body",(0,0,9));head=part("head",(0,0,16));armA=part("armA",(6,0,14));armB=part("armB",(-6,0,14));boots=[part("footA",(3,0,5)),part("footB",(-3,0,5))]
    look={
        "courier":("#F3BE91","#764732","#3B5961","#109783","#E89141","#A26137"),
        "tide":("#B77C58","#2F303B","#263F65","#437B9A","#E8DBAB","#805744"),
        "grove":("#D99E7A","#503D33","#4D7045","#82934F","#E2A954","#79583A"),
        "spark":("#A86B51","#372C42","#404A67","#687897","#D86C41","#774D48")}[key]
    skin,hair,cloth,shade,accent,leather=look
    # Boots have soles, heel blocks, uppers, cuffs, laces and a forward toe.
    for i,(b,x) in enumerate(zip(boots,[3,-3])):
        rounded(b,x-2,x+2,-3,3,0,1,DEEP);rounded(b,x-2,x+2,-3,3,1,3,leather)
        b.box(x-2,x+2,-2,2,3,6,cloth);b.box(x-2,x+2,-2,2,4,5,accent)
        front(b,x-1.25,x+1.25,1.5,1.9,-3.03,BRASS);b.box(x-2,x+2,-1,2,5,8,cloth)
    rounded(body,-5,5,-3,3,6,15,cloth);body.box(-3,3,-2,2,14,17,skin)
    # Discrete layered sleeves and fingerless mitts; no rigid arm welded into torso.
    for arm,x in [(armA,6),(armB,-7)]:
        arm.box(x-1,x+2,-2,2,11,15,shade);arm.box(x-1,x+2,-2,2,10,12,accent)
        arm.box(x,x+2,-1,2,8,10,skin);arm.box(x-1,x+2,-2,2,7,9,leather);front(arm,x-.4,x+1,7.4,8.5,-2.02,skin)
    headbase(head,skin,hair);P.update(faces(key,{"courier":"#356B69","tide":"#57433F","grove":"#586C42","spark":"#513847"}[key],"#C47768"))
    if key=="courier":
        # Leather aviator cap with joined brass goggles sitting ABOVE, not over the face.
        rounded(head,-7,7,-5,6,23,28,leather);rounded(head,-6,6,-4,5,27,29,"#B87840")
        head.boxm(6,8,-2,4,19,25,leather);head.boxm(6,7,-2,4,17,20,"#694435")
        head.box(-6,6,-6,-4,23,25,"#5D4139");disc(head,-4,-7,25,2,GOLD,"#51A3AA");disc(head,3,-7,25,2,GOLD,"#51A3AA")
        head.box(-1,1,-7,-5,25,26,BRASS);dots(head,[(-5,-8,26),(2,-8,26)],"#D9FFFF")
        for x in (-6,-3,1,5):head.put(x,-5,23,hair)
        body.box(-5,5,-4,4,13,16,"#178F83");body.box(-3,3,-4,4,15,17,"#55B8A1")
        plate(body,[(-4,14),(5,14),(7,11),(5,10),(0,11),(-6,10),(-7,12)],-4,4,shade)
        plate(body,[(-4,14),(5,14),(6,10),(5,7),(1,8),(-1,6),(-2,9),(-6,8)],3,5,"#0F766D")
        plate(body,[(-3,14),(-1,14),(-1,10),(-4,8),(-5,9)],4,6,"#E9A23D")
        line(body,(-4,-5,14),(3,-5,8),1,leather);body.box(2,7,-2,4,7,11,"#915738");body.box(2,7,-3,4,10,12,"#C9834A")
        front(body,3.5,5.1,9.55,10.75,-3.01,GOLD);back(body,-3,3,9,12,5.1,"#EBBB76")
        line(body,(-3,5,12),(0,5,10),1,BONE);line(body,(0,5,10),(2,5,12),1,BONE)
        body.box(1,3,-4,-3,12,14,BONE)
        # Nape of an aviator cap: bound leather rim over chestnut waves, a ribbon
        # fastening at one side. The cap's stitched centre seam reads from above.
        rounded(head,-6,6,3,6,18,24,"#815134");head.box(-4,4,4,6,17,19,"#85543A")
        head.box(-6,-3,3,6,16,20,"#73412E");head.box(3,6,3,6,17,20,"#915E3D")
        back(head,-5,5,22.7,23.5,6.0,"#583B31");head.box(2,3,-3,5,28,29,"#C78D55")
        plate(body,[(-3,15),(1,15),(-1,12),(-4,10),(-7,8),(-8,9),(-5,12)],4,6,"#127F78")
    if key=="tide":
        # Tall naval bicorne with a gold piping line, a split crest and swept feather.
        plate(head,[(-10,24),(-9,28),(-6,27),(-3,30),(0,31),(3,30),(6,27),(9,29),(10,26),(6,24),(3,23),(-5,23)],-4,5,"#233751")
        plate(head,[(-10,25),(-9,27),(-6,26),(-2,29),(1,30),(5,27),(8,27),(9,28),(9,26),(5,25),(1,28),(-2,27),(-5,25)],-5,-4,"#E5B65B")
        plate(head,[(4,27),(5,30),(10,33),(10,31),(8,30),(9,30),(7,28)],1,3,"#F3E4CC")
        disc(head,0,-6,26,1,GOLD,"#BB6551");head.boxm(5,7,1,5,18,24,hair)
        body.box(-5,5,-4,3,7,15,"#2D567B");plate(body,[(-4,14),(0,11),(4,14),(2,15),(-2,15)],-5,-3,BONE)
        for z in (8,10,12):
            dots(body,[(-3,-5,z),(2,-5,z)],GOLD)
        body.boxm(4,7,-2,3,14,16,GOLD);body.boxm(4,7,-1,3,13,14,BONE)
        plate(body,[(-5,8),(5,8),(5,4),(2,5),(0,7),(-2,5),(-5,4)],1,4,cloth)
        # Life ring mounted on rear — central hole and alternating canvas bands.
        for x in range(-5,6):
            for z in range(-5,6):
                d=x*x+z*z
                if 8<=d<=25:body.box(x,x+1,4,6,10+z,11+z,"#C45C43" if (abs(x)<2 or abs(z)<2) else "#F9E0AE")
        armA.box(5,9,-3,2,7,11,"#875E41");armA.box(5,9,-3,2,8,9,GOLD);front(armA,5.5,8.5,9.1,10.3,-3.1,"#CEE5DA")
        head.box(-6,6,3,6,18,24,hair);rounded(head,-3,3,5,8,17,21,"#3E3740")
        head.box(-2,2,6,8,16,18,BRASS);plate(head,[(-2,17),(2,17),(3,13),(1,12),(-1,13)],5,7,"#453B41")
        plate(head,[(-3,23),(0,23),(1,22),(-2,22)],-6,-5,"#514241")
    if key=="grove":
        # Soft hood, two asymmetric bifurcated wooden branches, each tipped with folded leaves.
        rounded(head,-7,7,-4,6,22,27,"#658551");rounded(head,-5,6,-3,5,26,28,"#8B9B59")
        head.boxm(5,7,-4,4,18,24,"#506E45")
        for sign in (-1,1):
            line(head,(sign*4,1,26),(sign*6,1,30),1,"#805B3B");line(head,(sign*6,1,30),(sign*9,1,32 if sign<0 else 31),1,"#B18958")
            line(head,(sign*7,1,30),(sign*7,1,33 if sign<0 else 31),1,"#CFAD74")
            plate(head,[(sign*7,29),(sign*10,28),(sign*12,30),(sign*9,31)],0,2,"#779A55")
        plate(head,[(-5,24),(-2,26),(1,25),(3,23),(1,23),(-1,24),(-2,22)],-6,-4,hair)
        for sign in (-1,1):
            head.box(sign*5-1,sign*5+1,3,6,15,22,hair);head.box(sign*5-1,sign*5+1,4,6,14,16,GOLD)
        for z in range(7,15):
            w=4+(15-z)//2
            rounded(body,-w,w,-4,5,z,z+1,["#6E8950","#829455","#839B5B","#677E48"][(z//2)%4])
        for x in (-5,-2,2,5):
            line(body,(x,-5,10),(x+1,-5,8),1,"#CFBC74")
        disc(body,0,-6,13,2,BRASS,"#6E9D80");body.box(-4,4,4,7,8,13,leather);body.box(-5,5,5,8,12,15,"#BB935B")
        back(body,-4,-3,12,15,8,"#644D3B");back(body,3,4,12,15,8,"#644D3B")
        armB.box(-9,-6,0,3,5,9,"#77563E");armB.box(-9,-6,-1,3,6,8,"#EDBF70");armB.box(-10,-5,-1,4,5,6,BRASS)
        rounded(head,-6,6,3,6,17,24,"#59784C")
        plate(head,[(-6,22),(6,22),(5,18),(2,16),(0,14),(-1,17),(-5,17)],5,7,"#759252")
        line(head,(-4,6,21),(0,6,16),1,"#8E9F60");dots(head,[(-4,-6,19),(-3,-6,18),(3,-6,19)],"#B67754")
    if key=="spark":
        # Swept copper undercut, clipped side, asymmetric raised goggles and a ratchet backpack.
        rounded(head,-7,7,-4,5,22,26,"#873F39")
        for x,z,y,hi in [(-6,26,1,30),(-4,26,-1,31),(-2,25,-2,30),(0,24,-3,29),(2,23,-3,27)]:
            plate(head,[(x-1,z),(x+3,z),(x+4,hi-1),(x+3,hi),(x+1,hi-1)],y,y+4,"#BC6347" if x%4 else "#E58D53")
        head.box(5,7,-1,4,18,23,"#473942");head.box(6,8,-1,4,20,23,"#707D86");head.put(7,-2,21,GOLD)
        head.box(-6,5,-6,-4,23,24,INK);disc(head,-4,-7,24,2,BRASS,"#91D2CB");disc(head,2,-7,25,2,GOLD,"#6D9BB2")
        body.box(-4,4,-4,2,7,14,"#485973");body.box(-4,-2,-5,-3,11,16,leather);body.box(2,4,-5,-3,11,16,leather)
        front(body,-1.8,2,10.7,13.2,-4.2,"#96A4A4");front(body,-1.6,1.7,12.4,12.8,-4.4,INK)
        body.box(-5,5,-4,4,8,9,leather);body.box(-4,4,3,6,8,15,"#495B69")
        for x in (-3,2):
            rounded(body,x-1,x+2,5,8,7,16,"#B47B50");body.box(x-1,x+2,5,8,9,10,GOLD);body.box(x-1,x+2,5,8,13,14,"#66585A")
        # Mechanical right arm: broad pauldrons, banded forearm, two independent claws.
        armA.box(5,9,-3,3,12,16,"#D39960");armA.box(6,9,-2,3,10,12,"#576673");armA.box(6,10,-3,3,7,11,"#BE7B4F")
        armA.box(6,10,-3,3,9,10,"#708591");armA.box(6,7,-3,0,5,7,DEEP);armA.box(9,10,-3,0,5,8,DEEP);front(armA,6.5,8.2,12,14,-3.2,"#9AC9C4")
        armB.box(-8,-5,-2,2,11,12,"#DE8C55");body.box(-6,-4,-1,3,6,10,leather)
        head.box(-6,6,3,6,17,24,"#513843");head.box(-5,4,4,6,21,25,"#81433D")
        plate(head,[(-6,24),(5,24),(4,22),(2,21),(1,23),(-1,21),(-3,22),(-6,20)],5,7,"#9F4F3F")
        head.box(-6,6,4,6,19,20,"#3D3340");back(head,2.1,4.2,19.1,20,6.1,"#C48C5B")
    return P
def export(key):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    P=figure(key);mat=bpy.data.materials.new("Dyed voxel enamel");mat.use_nodes=True
    p=mat.node_tree.nodes.get("Principled BSDF");p.inputs["Roughness"].default_value=.85
    col=mat.node_tree.nodes.new("ShaderNodeVertexColor");col.layer_name="col";mat.node_tree.links.new(col.outputs["Color"],p.inputs["Base Color"])
    obs={};rig=bpy.data.objects.new("human_"+key,None);bpy.context.collection.objects.link(rig)
    for n,part in P.items():
        d=part.mesh(unit=UNIT);mb=MeshBuilder();mb.add(d["V"],d["F"],0,vcolors=d["C"],smooth=False)
        ob=mb.build(n,[mat],None);ob.data.color_attributes.active_color=ob.data.color_attributes["col"];obs[n]=ob
    for n,ob in obs.items():
        parent="head" if n.startswith("face_") else "body" if n.startswith("arm") else None
        ob.parent=obs[parent] if parent else rig
        ob.location=tuple((np.array(P[n].pivot)-(np.array(P[parent].pivot) if parent else 0))*UNIT)
    path=OUT/("human_"+key+".glb")
    bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",export_yup=True,export_vertex_color="ACTIVE",export_animations=False,export_cameras=False,export_lights=False)
    for n in ["face_tight","face_happy"]:obs[n].hide_render=True
    sc=bpy.context.scene;sc.render.engine="CYCLES";sc.cycles.samples=12;sc.cycles.use_denoising=True
    sc.render.film_transparent=True;sc.render.resolution_x=sc.render.resolution_y=420;sc.render.resolution_percentage=100
    sc.view_settings.view_transform="AgX";world=bpy.data.worlds.new("Sky bounce");sc.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.70,.82,1,1);world.node_tree.nodes['Background'].inputs[1].default_value=.65
    def light(loc,power,size,color):
        lamp=bpy.data.lights.new("Softbox","AREA");lamp.energy=power;lamp.shape="DISK";lamp.size=size;lamp.color=color
        obj=bpy.data.objects.new("Softbox",lamp);sc.collection.objects.link(obj);obj.location=loc;obj.rotation_euler=(Vector((0,0,.5))-obj.location).to_track_quat("-Z","Y").to_euler()
    light((-2,-3,3.8),210,3,(1,.9,.75));light((2.5,1.4,2.0),110,2,(.65,.82,1))
    camera=bpy.data.cameras.new("Portrait");camera.type="ORTHO";camera.ortho_scale=1.17
    cam=bpy.data.objects.new("Portrait",camera);sc.collection.objects.link(cam);sc.camera=cam
    for label,yaw,pitch in [("front",0,10),("side",90,10),("back",180,10),("game",22,51),("portrait",-19,12)]:
        a,b=math.radians(yaw),math.radians(pitch);d=Vector((math.sin(a)*math.cos(b),-math.cos(a)*math.cos(b),math.sin(b)))
        camera.ortho_scale=.80 if label=="portrait" else 1.17
        focus=Vector((0,0,.67 if label=="portrait" else .50));cam.location=focus+d*4;cam.rotation_euler=(-d).to_track_quat("-Z","Y").to_euler()
        sc.render.filepath=str(OUT/("human_"+key+".png") if label=="portrait" else QA/(key+"-"+label+".png"));bpy.ops.render.render(write_still=True)
    print("FIGURE",key,sum(len(p.vox) for p in P.values()),path.stat().st_size,flush=True)
for key in ["courier","tide","grove","spark"]:export(key)
