"""Mechanical A.4: 3 mm walnut, 3 mm smoked PMMA, full-area optical stack.
All profiles use u=horizontal, z=up, independent of original DXF nesting.
Laser contours and installed solids share these profiles. Nominal mm;
measure stock and calibrate laser kerf on a coupon before the full cut.
"""
from pathlib import Path
import json, logging
import ezdxf, numpy as np, trimesh as tm
from shapely.geometry import Polygon, Point, box, LineString
from shapely.ops import unary_union
from shapely.affinity import translate
from design import CONTROL_X
from mechanical_spec import usb_profile
logging.disable(logging.CRITICAL)
root=Path(__file__).resolve().parents[1]/'mechanical'
original=list(ezdxf.readfile(root/'reference/wood and plexi.dxf').modelspace())
WOOD=PLEXI=3.0
OPTICS=.2
GRID_TOP=16.7
FRONT_BOTTOM=GRID_TOP+OPTICS
HEIGHT=FRONT_BOTTOM+WOOD
CLEARANCE=.08
DIFFUSER_SIZE=90.0
def original_vertices(i):
    points=[(v.dxf.location.x,v.dxf.location.y,v.dxf.bulge) for v in original[i].vertices]
    if points[0][:2]==points[-1][:2]:points.pop()
    return points
def polygon(i):
    return Polygon([(v.x,v.y) for v in ezdxf.path.make_path(original[i]).flattening(.005)])
# Change only retained 96 mm front's finger depth (3.2 -> 3 mm).
# Keep exact original R20 aperture and independent R20 plexi contour.
frame_points=[]
for x,y,bulge in original_vertices(19):
    q=[x-976,y-630]
    q=[np.copysign(45.,v) if abs(abs(v)-44.8)<.001 else v for v in q]
    frame_points.append((q[0]+976,q[1]+630,bulge))
frame_outline=Polygon([(x-976,y-630) for x,y,_ in frame_points])
window=translate(polygon(20),xoff=-976,yoff=-630)
frame=frame_outline.difference(window)
corner_axes=[(sx*36.51917,sy*36.51917) for sx in [-1,1] for sy in [-1,1]]
mid_axes=[(0,36.51917),(36.51917,0),(0,-36.51917),(-36.51917,0)]
for x,y in corner_axes:frame=frame.difference(Point(x,y).buffer(1.6,resolution=48))
plexi=translate(polygon(4),xoff=-978,yoff=-495)
for x,y in mid_axes:plexi=plexi.difference(Point(x,y).buffer(1.6,resolution=48))
def extrude(shape,h,z=0):
    mesh=tm.creation.extrude_polygon(shape,h,engine='triangle');mesh.apply_translation([0,0,z])
    assert mesh.is_watertight and mesh.is_volume
    return mesh
extrude(frame,WOOD,FRONT_BOTTOM).export(root/'wood-front-fit.stl')
extrude(plexi,PLEXI,FRONT_BOTTOM).export(root/'plexi-fit.stl')
# Actual printed side-wall screw axes: Y walls u=+/-35, X walls +/-32.5;
# all at Z=5.85. The old DXF profiles had been both swapped and inverted.
panels={};metadata=[]
locations={'usb':(976,705),'button':(976,755),'opposite-usb':(976,680),'opposite-button':(976,730)}
definitions=[('usb',1,1,35),('button',0,1,32.5),('opposite-usb',1,-1,35),('opposite-button',0,-1,32.5)]
for name,normal,sign,hole_u in definitions:
    shape=box(-45,0,45,HEIGHT)
    # Complementary vertical corner joints. No comb along the bottom edge.
    for j in range(5):
        if (j%2==0)!=(normal==0):continue
        lo=j*HEIGHT/5+(CLEARANCE/2 if j else 0)
        hi=(j+1)*HEIGHT/5-(CLEARANCE/2 if j<4 else 0)
        shape=shape.union(box(-48,lo,-44.99,hi)).union(box(44.99,lo,48,hi))
    along=1-normal
    positions=sorted(set(np.asarray(frame_outline.exterior.coords)[:,along]))
    for low,high in zip(positions,positions[1:]):
        u=(low+high)/2;a,b=np.zeros(2),np.zeros(2)
        a[along]=b[along]=u;a[normal]=sign*45.01;b[normal]=sign*47.99
        if frame_outline.intersection(LineString([a,b])).length>1e-5:
            shape=shape.difference(box(low-CLEARANCE/2,FRONT_BOTTOM-CLEARANCE/2,high+CLEARANCE/2,HEIGHT+.1))
    for u in [-hole_u,hole_u]:shape=shape.difference(Point(u,5.85).buffer(1.6,resolution=48))
    if name=='usb':
        shape=shape.difference(usb_profile())
        shape=shape.difference(box(-CONTROL_X-2.1,6.89,-CONTROL_X+2.1,9.69))
    assert shape.geom_type=='Polygon',name
    panels[name]=shape;mesh=extrude(shape,WOOD)
    T=np.zeros((4,4));T[3,3]=1;T[along,0]=1;T[2,1]=1;T[normal,2]=sign;T[normal,3]=sign*45
    mesh.apply_transform(T);mesh.export(root/f'wood-{name}-fit.stl')
    metadata.append({'part':name,'wall_axis':'XY'[normal],'wall_sign':sign,'thickness_mm':WOOD,
        'screw_centres_local_uz_mm':[[-hole_u,5.85],[hole_u,5.85]],
        'DXF_origin_mm':locations[name],'installed_transform':T.tolist()})
# Two representative 0.1 mm visual layers. Only the measured combined 0.2 mm
# is specified, not the paper/PVC individual thickness ratio.
optical=box(-DIFFUSER_SIZE/2,-DIFFUSER_SIZE/2,DIFFUSER_SIZE/2,DIFFUSER_SIZE/2)
optical_holes=[]
for x,y in mid_axes+corner_axes:
    # Copy the grid's existing clearance diameters (two X-mid bores are 4 mm).
    diameter=4.0 if y==0 else 3.2
    optical=optical.difference(Point(x,y).buffer(diameter/2,resolution=48))
    optical_holes.append({'center_xy_mm':[x,y],'diameter_mm':diameter})
extrude(optical,OPTICS/2,GRID_TOP).export(root/'diffuser-paper-fit.stl')
extrude(optical,OPTICS/2,GRID_TOP+OPTICS/2).export(root/'filter-film-fit.stl')
# No separate shims: the full-area 0.2 mm sheet now supports all eight axes.
def make_dxf(include_wood,include_plexi):
    doc=ezdxf.new('R2010');doc.units=ezdxf.units.MM;ms=doc.modelspace()
    for name,color in [('WALNUT_3MM_CUT',3),('SMOKED_PMMA_3MM_CUT',4)]:doc.layers.new(name,dxfattribs={'color':color})
    def line(coords,layer,bulges=False):ms.add_lwpolyline(coords,format='xyb' if bulges else 'xy',close=True,dxfattribs={'layer':layer})
    if include_wood:
        layer='WALNUT_3MM_CUT';line(frame_points,layer,True);line(original_vertices(20),layer,True)
        for x,y in corner_axes:ms.add_circle((x+976,y+630),1.6,dxfattribs={'layer':layer})
        for name,shape in panels.items():
            ox,oy=locations[name];line([(x+ox,y+oy) for x,y in list(shape.exterior.coords)[:-1]],layer)
            for hole in shape.interiors:
                poly=Polygon(hole)
                if abs(poly.area-np.pi*1.6**2)<.01:ms.add_circle((poly.centroid.x+ox,poly.centroid.y+oy),1.6,dxfattribs={'layer':layer})
                else:line([(x+ox,y+oy) for x,y in list(hole.coords)[:-1]],layer)
    if include_plexi:
        layer='SMOKED_PMMA_3MM_CUT';line(original_vertices(4),layer,True)
        for x,y in mid_axes:ms.add_circle((x+978,y+495),1.6,dxfattribs={'layer':layer})
    assert not doc.audit().has_errors
    return doc
make_dxf(True,True).saveas(root/'TiltBox-Voice-A-wood-and-plexi.dxf')
make_dxf(True,False).saveas(root/'TiltBox-Voice-A-walnut-3mm.dxf')
make_dxf(False,True).saveas(root/'TiltBox-Voice-A-smoked-plexi-3mm.dxf')
# Deliberately not a laser file: PVC must be cut with a knife/scissors.
svg_holes=''.join(f'<circle cx="{h["center_xy_mm"][0]+55}" cy="{55-h["center_xy_mm"][1]}" r="{h["diameter_mm"]/2}"/>' for h in optical_holes)
(root/'diffuser-manual-cut.svg').write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="110mm" height="115mm" viewBox="0 0 110 115"><title>Handsnijmal, papier en donkere PVC-folie, samen 0.2 mm</title><g fill="none" stroke="black" stroke-width="0.15"><rect x="10" y="10" width="90" height="90"/>{svg_holes}</g><g font-family="sans-serif" font-size="3" text-anchor="middle"><text x="55" y="6">90 x 90 mm - afdrukken op 100%</text><text x="55" y="105">8 montagegaten zoals de grid - ledvakjes NIET uitsnijden</text><text x="55" y="110">HANDMATIG SNIJDEN - PVC NOOIT LASEREN</text><text x="55" y="114">Papier + donkerfolie samen 0.2 mm</text></g></svg>''')
report={'mechanical_revision':'A.4','PCB_revision':'A.2','wood_material':'walnut','wood_thickness_mm':WOOD,
    'plexi_material':'smoked PMMA','plexi_thickness_mm':PLEXI,'diffuser_combined_thickness_mm':OPTICS,
    'diffuser_size_mm':[DIFFUSER_SIZE,DIFFUSER_SIZE],'optical_individual_layers_mm':[.1,.1],
    'individual_layer_ratio_visual_only':True,'grid_top_z_mm':GRID_TOP,'optical_stack_z_mm':[GRID_TOP,FRONT_BOTTOM],
    'plexi_z_mm':[FRONT_BOTTOM,FRONT_BOTTOM+PLEXI],'wood_frame_z_mm':[FRONT_BOTTOM,HEIGHT],
    'overall_envelope_mm':[96,96,HEIGHT],'front_spacers':{'count':0,'reason':'Full-area optical sheet supports all eight axes'},
    'diffuser_holes':optical_holes,'grid_material':'matte opaque black',
    'top_fasteners':{'count':8,'standard':'ISO 7380-1','screws':'M3x14','nominal_thread_engagement_mm':4.2},
    'joint_clearance_mm':CLEARANCE,'panels':metadata,'screen_corner_radius_mm':20,
    'side_fasteners':{'insert_count':8,'thread':'M3','pilot_mm':4.0,'max_insert_length_mm':4.5,'screws':'8 x M3x6'},
    'PVC_laser_cutting_allowed':False,'physical_test_performed':False}
(root/'optical-stack.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
