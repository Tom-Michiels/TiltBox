"""Adapt the original watertight TiltBox meshes, preserving their exterior/grid.
Requires trimesh, numpy, manifold3d, matplotlib. All sizes are mm.
"""
from pathlib import Path
import json,math,shutil
import numpy as np
import trimesh as tm
from shapely.geometry import box as box2,Point,Polygon
from design import ROOT,PARTS,CONTROL_X
from mechanical_spec import usb_profile,USB_Z,USB_COLLAR_Y
ORIGINAL=ROOT/'mechanical/reference'
OUT=ROOT/'mechanical';OUT.mkdir(exist_ok=True)
def box(size,center):
    m=tm.creation.box(extents=size);m.apply_translation(center);return m
def cylinder(radius,height,center):
    m=tm.creation.cylinder(radius=radius,height=height,sections=96);m.apply_translation(center);return m
def union(parts):return tm.boolean.union(parts,engine='manifold')
def diff(mesh,cuts):return tm.boolean.difference([mesh,union(cuts)],engine='manifold')
def along_y(profile,lo,hi):
    m=tm.creation.extrude_polygon(profile,hi-lo,engine='triangle')
    m.apply_transform(np.array([[1,0,0,0],[0,0,1,lo],[0,1,0,0],[0,0,0,1.]]))
    return m
body_original=tm.load(ORIGINAL/'tilt_box.stl')
grid_original=tm.load(ORIGINAL/'tilt box grid.stl')
assert body_original.is_watertight and grid_original.is_watertight

# A larger internal pocket removes obsolete breakout-board cradles, not the outside.
# Keep 1.8 mm floor and 5 mm nominal exterior walls/finger joints.
body=diff(body_original,[box((80,80,20),(0,0,11.8))])
# Close the obsolete module cut-out in the original floor; the revised cell must
# never be exposed through that old opening. The final acoustic hole is cut later.
body=union([body,box((80,80,1.8),(0,0,.9)),
    # Obsolete USB-module opening and its narrow retention slots in +Y wall.
    box((20,5,11.7),(-22.38,42.5,5.85)),
    # Obsolete large side opening from the original discrete-module design.
    box((5,17.6,11.7),(42.5,0,5.85))])
mount=[(0,36.51917),(36.51917,0),(0,-36.51917),(-36.51917,0)]
# Eight original fastening axes remain; corner screws pass through PCB clearance holes.
corners=[(sx*36.51917,sy*36.51917) for sx in [-1,1] for sy in [-1,1]]
posts=[cylinder(3.6,8.5,(x,y,5.85)) for x,y in mount+corners]
# Closed sound chimney isolates the mic from internal vibration and LED/power airflow.
mic=next(p for p in PARTS if p['ref']=='U8')
mx,my=mic['xy'][0],-mic['xy'][1]-.71
posts.append(cylinder(2.0,8.2,(mx,my,5.8)))
body=union([body]+posts+[
    # Inner 1 mm of the tunnel wraps the metal shell. Starts beyond the PCB
    # edge, overlaps the main wall, and has a 0.20 mm nominal assembly gap.
    box((12.8,1.2,7.1),(CONTROL_X,39.65,8.15))])
# Requested 4.0 mm insert pilot, open only from the PCB side. PCB holes stay 3.2.
cuts=[cylinder(2.0,6,(x,y,7.25)) for x,y in mount+corners]
cuts += [cylinder(.9,12,(mx,my,5.5))]
# USB-C opening: connector is on PCB underside, mouth faces +Y.
# Rounded boot tunnel, followed by the close-fitting final 1 mm collar.
cuts += [along_y(usb_profile(),USB_COLLAR_Y[1],49),
         along_y(usb_profile(True),38.9,USB_COLLAR_Y[1]+.001)]
# Both controls face +Y, one centred in each gap between screw axes.
cuts += [box((5.4,6.53,3.8),(-CONTROL_X,37.765,8.29)),box((4.2,7,2.8),(-CONTROL_X,44.5,8.29))]
# Reserved antenna recess: leave 1.2 mm plastic to the outside. The flex is
# vertical, below the PCB, not stuck onto copper or the battery pouch.
# FXP840 drawing maximum envelope 14.7 x 5.3 x 0.34 mm, 55 mm micro-coax.
cuts += [box((5.8,18,7.0),(40.9,-12,5.5))]
body=diff(body,cuts)

# Top grid remains the original part. Revision A.1 no longer has per-LED
# capacitors. Retain only the microphone and its own local decoupling clearance.
gridcuts=[]
for part in PARTS:
    if part['side']=='F' and part['ref'].startswith('C'):
        x,y=part['xy'];gridcuts.append(box((2.0,1.3,.9),(x,-y,.35)))
gridcuts.append(box((3.4,4.2,1.5),(mx,-mic['xy'][1],.65)))
grid=diff(grid_original,gridcuts)

# A.3: bring the 4 mm optical cell walls to the 5 mm rim, supporting the
# diffuser without a 1 mm inter-cell gap. Lower component clearances unchanged.
section=grid.section(plane_origin=[0,0,3.5],plane_normal=[0,0,1])
loops=sorted([Polygon(p[:,:2]) for p in section.discrete],key=lambda p:-p.area)
profile=loops[0]
for hole in loops[1:]:profile=profile.difference(hole)
# Clip within the optical opening, with a small overlap into its rim. This
# avoids coincident duplicate surfaces around the original screw bores.
profile=profile.intersection(box2(-32.83,-32.83,32.83,32.83))
cap=tm.creation.extrude_polygon(profile,float(grid.bounds[1,2])-3.8,engine='triangle')
cap.apply_translation([0,0,3.8]);grid=union([grid,cap])

# Battery: 45 x 26 x 5 mm maximum protected cell envelope, retained with electrically
# insulating foam adhesive, away from screw/connector envelopes.
# Do not squeeze the pouch. TH1 is above the cell; use the specified soft thermal pad.
battery_envelope=box((45,26,5),(-2,-19.5,4.6))
antenna_envelope=box((.34,14.7,5.3),(43.58,-12,4.85))
antenna_envelope.export(OUT/'antenna-fit-model.stl')
# Plunger body starts at switch actuator; flange is retained behind the wall.
plunger=union([box((9.2,3.6,2.4),(1.6,0,0)),box((1.1,4.8,3.4),(-2.3,0,0))])
plunger.apply_translation((0,0,1.7))
# Print mesh is already oriented along +Y, avoiding viewer-only transforms.
plunger.apply_transform(tm.transformations.rotation_matrix(math.pi/2,[0,0,1]))

for name,mesh in [('TiltBox-Voice-A-body.stl',body),('TiltBox-Voice-A-grid.stl',grid),('TiltBox-Voice-A-button.stl',plunger)]:
    assert mesh.is_watertight and mesh.volume>0,name
    mesh.export(OUT/name)
    reloaded=tm.load(OUT/name)
    assert reloaded.is_watertight and reloaded.is_volume,('STL round-trip',name)
shutil.copy2(ORIGINAL/'wood and plexi.dxf',OUT/'original-wood-and-plexi.dxf')

# Board fit model includes all mounting bores and acoustic bore. The overlap test
# excludes intended zero-area contacts (support faces), and uses physical bodies.
shape=box2(-37,-39,37,39).union(box2(-39,-37,39,37))
for x in [-37,37]:
    for y in [-37,37]:shape=shape.union(Point(x,y).buffer(2,resolution=32))
for x,y in mount+corners:shape=shape.difference(Point(x,y).buffer(1.6,resolution=32))
shape=shape.difference(Point(mx,my).buffer(.25,resolution=32))
board=tm.creation.extrude_polygon(shape,1.6,engine='triangle');board.apply_translation((0,0,10.1))
board.export(OUT/'pcb-fit-model.stl')
fitbody=tm.boolean.intersection([body,board],engine='manifold')
fitbattery=tm.boolean.intersection([body,battery_envelope],engine='manifold')
assert fitbody.volume<.02,('PCB/body collision',fitbody.volume)
assert fitbattery.volume<.02,('Battery/body collision',fitbattery.volume)
placed_grid=grid.copy();placed_grid.apply_translation((0,0,11.7))
report={'board_mm':[78,78,1.6],'board_bottom_z_mm':10.1,'board_top_z_mm':11.7,
 'outside_original_mm':body_original.extents.tolist(),'outside_revised_mm':body.extents.tolist(),
 'grid_pitch_mm':58.3/7,'mount_centres_mm':mount+corners,'mic_port_mm':[mx,my],
 'mic_pcb_hole_mm':.5,'mic_case_inlet_mm':1.8,'battery_max_mm':[45,26,5],
 'battery_center_mm':[-2,-19.5,4.6],'PCB_case_collision_mm3':fitbody.volume,
 'antenna_mpn':'Taoglas FXP840.07.0055B','antenna_center_mm':[43.58,-12,4.85],
 'antenna_envelope_mm':[.34,14.7,5.3],'antenna_coax_mm':55,
 'antenna_to_pcb_body_min_mm':math.hypot(43.41-39,10.1-7.5),
 'antenna_to_battery_min_mm':43.41-20.5,
 'insert_pilot_diameter_mm':4.0,'PCB_mount_hole_diameter_mm':3.2,
 'mechanical_revision':'A.4','optical_cell_wall_top_mm':5.0,
 'grid_material':'matte opaque black PETG or PLA',
 'usb_opening_center_mm':[CONTROL_X,42,USB_Z],
 'usb_outer_opening_whR_mm':[12,6.3,1.5],
 'usb_inner_opening_whR_mm':[9.34,3.56,1.2],
 'usb_collar_y_mm':list(USB_COLLAR_Y),'usb_shell_nominal_gap_mm':.2,
 'button_install_translation_mm':[-CONTROL_X,42.53,6.59],
 'button_motion_axis':[0,1,0], 'controls_side':'+Y',
 'controls_screw_midpoint_x_mm':[-CONTROL_X,CONTROL_X],
 'button_actuator_rest_gap_mm':.2,'button_retention_lip_mm':.3,
 # Module is on B.Cu: the connector is +X from U1, not -X.
 # Cross-checked against placed manufacturer STEP socket NAUO222.
 'antenna_ufl_center_mm':[19.0063,13.0066,7.4],
 'antenna_ufl_socket_face_mm':[19.0063,13.0066,7.965],
 'antenna_ufl_source':'Placed U1 manufacturer STEP socket NAUO222; datasheet figure 10-2',
 'antenna_coax_waypoints_mm':[[19.0063,13.0066,7.4],[22,10,7.0],[27,3,6],[34,-7,4.85],[43.15,-12,4.85]],
 'battery_case_collision_mm3':fitbattery.volume,'STLs_watertight':True,
 'note':'Nominal CAD fit checked with 136 manufacturer component models; see component-collisions.json and accessory-fit.json. Physical tolerances, print shrinkage and the first assembled prototype still require inspection.'}
(OUT/'fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
# A compact visual check of actual CAD cross-sections, not an AI mock-up.
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axes=plt.subplots(1,3,figsize=(15,5))
for ax,z,title in zip(axes,[1,6,10.3],['Bottom / acoustic inlet','Internal pocket / battery envelope','PCB support plane']):
    sec=body.section(plane_origin=[0,0,z],plane_normal=[0,0,1])
    if sec:
        for seg in sec.discrete:ax.plot(seg[:,0],seg[:,1],color='#152c3f',lw=1)
    if z==6:
        ax.add_patch(plt.Rectangle((-24.5,-32.5),45,26,facecolor='#c6dfef',edgecolor='#3483b5',alpha=.6))
    if z==10.3:
        x,y=shape.exterior.xy;ax.plot(x,y,color='#2d8a50',lw=1)
    ax.set_title(title);ax.set_aspect('equal');ax.set_xlim(-47,47);ax.set_ylim(-47,47);ax.set_xlabel('mm');ax.grid(alpha=.15)
fig.tight_layout();fig.savefig(OUT/'fit-sections.png',dpi=150);plt.close(fig)
print(json.dumps(report))
