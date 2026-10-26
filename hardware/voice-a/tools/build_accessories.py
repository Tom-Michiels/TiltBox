"""A.4 purchased-part visual/fit models. Units mm; never print these models.
ISO 7380-1 head/shank envelopes; decorative thread grooves are not a thread gauge.
Cell is an unbranded maximum-envelope illustration, NOT a selected battery MPN.
"""
from pathlib import Path
import json,math
import numpy as np,trimesh as tm
from scipy.interpolate import make_interp_spline
from shapely.geometry import Point,Polygon

r=Path(__file__).resolve().parents[1]/'mechanical'
scene=tm.Scene();items=[]
def add(name,m,color,group,delta,metal=0,rough=.55):
    assert m.is_watertight and m.is_volume,name
    m.visual=tm.visual.TextureVisuals(material=tm.visual.material.PBRMaterial(
        name=name,baseColorFactor=color,metallicFactor=metal,roughnessFactor=rough))
    scene.add_geometry(m,node_name=name,geom_name=name)
    items.append({'node':name,'group':group,'explode_mm':delta,'bounds_mm':m.bounds.tolist()})
def box(size,center):
    m=tm.creation.box(extents=size);m.apply_translation(center);return m
def roundbox(size,center,radius):
    ball=tm.creation.icosphere(subdivisions=2,radius=radius)
    points=np.vstack([ball.vertices+np.array([sx,sy,sz])*(np.array(size)/2-radius)
                      for sx in [-1,1] for sy in [-1,1] for sz in [-1,1]])
    m=tm.convex.convex_hull(points);m.apply_translation(center);return m
def screw(length):
    # ISO maximum head Ø5.7 x 1.65, AF2 hex recess >=1.04 deep; M3 pitch .5.
    profile=[[0,-length],[1.18,-length],[1.5,-length+.28]]
    for z in np.arange(-length+.3,-.4,.5):
        profile.extend([[1.5,float(z)],[1.23,float(z+.18)],[1.5,float(z+.36)]])
    profile += [[1.5,0],[2.85,0],[2.85,.18],[2.80,.4],[2.65,.72],[2.4,1.10],[2.05,1.43],[1.68,1.65],[0,1.65]]
    m=tm.creation.revolve(np.array(profile),sections=96)
    hexagon=Polygon([(2/math.sqrt(3)*math.cos(a),2/math.sqrt(3)*math.sin(a)) for a in np.arange(6)*math.pi/3])
    cut=tm.creation.extrude_polygon(hexagon,2,engine='triangle');cut.apply_translation([0,0,.60])
    return tm.boolean.difference([m,cut],engine='manifold')
def ring(outer,inner,height):
    shape=Point(0,0).buffer(outer,resolution=48).difference(Point(0,0).buffer(inner,resolution=48))
    return tm.creation.extrude_polygon(shape,height,engine='triangle')

spec=json.loads((r/'optical-stack.json').read_text())
for i,h in enumerate(spec['diffuser_holes']):
    x,y=h['center_xy_mm'];corner=x!=0 and y!=0
    m=screw(14);m.apply_translation([x,y,19.9])
    add(f'top-screw-{i+1}',m,[190,195,201,255],'fasteners',[0,0,112 if corner else 98],.92,.25)
    ins=ring(2.3,1.53,5.7);ins.apply_translation([x,y,4.4])
    add(f'top-insert-{i+1}',ins,[186,139,53,255],'case',[0,0,-25],.8,.36)
for i,p in enumerate(spec['panels']):
    axis='XY'.index(p['wall_axis']);sign=p['wall_sign']
    for j,(u,z) in enumerate(p['screw_centres_local_uz_mm']):
        direction=np.eye(3)[axis]*sign
        T=tm.geometry.align_vectors([0,0,1],direction)
        T[:3,3]=[u,sign*48,z] if axis==1 else [sign*48,u,z]
        m=screw(6);m.apply_transform(T)
        add(f'side-screw-{i+1}-{j+1}',m,[190,195,201,255],'fasteners',(direction*36).tolist(),.92,.25)
        T[:3,3]=[u,sign*40.5,z] if axis==1 else [sign*40.5,u,z]
        ins=ring(2.2,1.53,4.5);ins.apply_transform(T)
        add(f'side-insert-{i+1}-{j+1}',ins,[186,139,53,255],'case',[0,0,-25],.8,.36)

add('battery-pouch',roundbox([43,24.8,4.6],[-1.5,-19.5,4.6],.65),[170,178,184,255],'battery',[0,0,-12],.88,.29)
add('battery-foil-seal',roundbox([44.7,25.7,.14],[-2,-19.5,4.6],.06),[188,193,197,255],'battery',[0,0,-12],.9,.42)
add('battery-terminal-insulation',roundbox([1.8,25,4.8],[-23.5,-19.5,4.6],.22),[187,133,25,255],'battery',[0,0,-12],.15,.32)
# Folded pouch edge detail stays inside the required cell envelope.
for name,y in [('front',-31.5),('rear',-7.5)]:
    add('battery-fold-'+name,box([41,.14,.25],[-1.5,y,6.55]),[119,127,137,255],'battery',[0,0,-12],.9,.38)
# Visible part of a PHR-2 mating housing, outside the PCB receptacle. The
# enclosed contacts/latch are intentionally simplified, not collision-tested.
housing=box([3.1,5.8,4.5],[-32.951,14,7.265])
cuts=[]
for y in [13,15]:
    cut=tm.creation.cylinder(radius=.58,height=3.5,sections=32)
    cut.apply_transform(tm.geometry.align_vectors([0,0,1],[1,0,0]));cut.apply_translation([-32.95,y,6.85]);cuts.append(cut)
housing=tm.boolean.difference([housing]+cuts,engine='manifold')
add('battery-plug-visible-housing',housing,[228,222,200,255],'battery',[0,0,0],0,.65)
paths={}
for name,dy,color in [('positive',0,[177,16,17,255]),('ground',2,[24,25,26,255])]:
    controls=np.array([[-24.4,-21+dy,4.6],[-28,-21+dy,4.6],[-30,-16+dy,4.6],[-30,-4+dy,4.6],
        [-31,4+dy,5.2],[-34.5,9+dy,6.4],[-36,12+dy,6.85],[-34.5,13+dy,6.85],[-32,13+dy,6.85]])
    if name=='ground':
        controls[:,0]+=np.array([0,0,-1.5,-1.5,-1.5,-1,0,0,0])
        controls[:,2]+=np.array([0,-1.4,-1.4,-1.4,-1.4,-1.4,-1.4,0,0])
    u=np.r_[0,np.cumsum(np.linalg.norm(np.diff(controls,axis=0),axis=1))]
    curve=make_interp_spline(u,controls,k=2);t=np.linspace(0,u[-1],240);xyz=curve(t)
    # A framed circular sweep, capped and manifold; 1.0 mm insulation OD.
    tangent=curve(t,1);tangent/=np.linalg.norm(tangent,axis=1)[:,None]
    n=np.cross(tangent,[0,0,1]);n/=np.linalg.norm(n,axis=1)[:,None];b=np.cross(tangent,n)
    angles=np.arange(12)*2*np.pi/12
    vertices=(xyz[:,None,:]+.5*(np.cos(angles)[None,:,None]*n[:,None,:]+np.sin(angles)[None,:,None]*b[:,None,:])).reshape(-1,3)
    faces=[]
    for i in range(len(xyz)-1):
        for j in range(12):
            a=i*12+j;bb=i*12+(j+1)%12;cc=bb+12;d=a+12;faces.extend([[a,bb,cc],[a,cc,d]])
    vertices=np.vstack([vertices,xyz[0],xyz[-1]])
    for j in range(12):faces.extend([[len(vertices)-2,(j+1)%12,j],[len(vertices)-1,(len(xyz)-1)*12+j,(len(xyz)-1)*12+(j+1)%12]])
    wire=tm.Trimesh(vertices=vertices,faces=faces);wire.fix_normals()
    add('battery-wire-'+name,wire,color,'battery',[0,0,0],0,.4)
    paths[name]={'points_mm':xyz.tolist(),'diameter_mm':1.0,'length_mm':float(np.linalg.norm(np.diff(xyz,axis=0),axis=1).sum()),'J2_pin':1 if name=='positive' else 2}
scene.export(r/'assembly-accessories.glb')
report={'revision':'A.4','units':'mm','objects':items,'battery_wire_paths':paths,
    'battery':'Unbranded 45x26x5 mm maximum-envelope illustration; actual protected pack still to be selected',
    'fasteners':{'standard':'ISO 7380-1','thread':'M3x0.5','head_diameter_mm':5.7,'head_height_mm':1.65,'hex_AF_mm':2,
        'top':{'count':8,'length_mm':14,'engagement_mm':4.2,'tip_to_pilot_bottom_mm':1.65},
        'side':{'count':8,'length_mm':6,'engagement_mm':3.0,'tip_to_insert_inner_end_mm':1.5},'threads':'simplified annular grooves, not manufacturing thread CAD'},
    'sources':['https://www.accu.co.uk/socket-button-screws/8103-SSB-M3-10-A2','https://www.jst-mfg.com/product/pdf/eng/ePH.pdf'],
    'physical_validation':False}
(r/'assembly-accessories.json').write_text(json.dumps(report,indent=2)+'\n')
print('Exported',len(items),'purchased-part meshes; eight M3x14 + eight M3x6 ISO 7380-1 screws.')
