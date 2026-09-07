"""Run once against the original Starfish, then edit Starfish.blend directly."""
import bpy
import math
import random
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(P/'Starfish.gltf'))
body=max((o for o in bpy.context.scene.objects if o.type=='MESH'),key=lambda o:len(o.data.vertices))
bpy.ops.object.select_all(action='DESELECT');body.select_set(True);bpy.context.view_layer.objects.active=body
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
body.name='Starfish_Body'
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=.00001)
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode='OBJECT')
# Increase thickness without changing the five-arm outline.
for v in body.data.vertices:
    v.co.z*=1.6
body.data.update()
bevel=body.modifiers.new('Soft edges','BEVEL');bevel.width=.022;bevel.segments=2
bevel.limit_method='ANGLE';bevel.angle_limit=.35
bpy.ops.object.modifier_apply(modifier=bevel.name)
def mat(name,c,rough=.7):
    m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*c,1);bs.inputs['Roughness'].default_value=rough
    return m
coral=mat('Coral Orange',(.83,.24,.12))
light=mat('Apricot Highlights',(.94,.39,.19))
dark=mat('Warm Coral Sides',(.63,.14,.10))
bottom=mat('Sand Underside',(.91,.65,.41))
grain=mat('Golden Papillae',(.98,.60,.29))
body.data.materials.clear()
for m in [coral,light,dark,bottom]:body.data.materials.append(m)
rng=random.Random(17)
for f in body.data.polygons:
    f.material_index=3 if f.normal.z<-.3 else 2 if f.normal.z<.3 else 1 if rng.random()<.20 else 0
bvh=BVHTree.FromPolygons([v.co for v in body.data.vertices],[list(p.vertices) for p in body.data.polygons])
spots=[]
for attempt in range(1800):
    x,y=rng.uniform(-1.3,1.3),rng.uniform(-1.3,1.3)
    point,normal,_,_=bvh.ray_cast(Vector((x,y,2)),Vector((0,0,-1)))
    if point is None or normal.z<.7:continue
    # Keep details off the thin arm edges and avoid overlapping dots.
    if any((point-p).length<.14 for p in spots):continue
    if any(bvh.ray_cast(Vector((x+dx,y+dy,2)),Vector((0,0,-1)))[0] is None
           for dx,dy in [(.06,0),(-.06,0),(0,.06),(0,-.06)]):continue
    spots.append(point)
    radius=rng.uniform(.020,.036)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=1,location=point+normal*.005)
    o=bpy.context.object;o.name='Starfish_Papilla_%02d'%len(spots)
    o.scale=(radius,radius,radius*.6);o.data.materials.append(grain)
    if len(spots)>=65:break
# Keep surface detail together as one editable mesh and one draw object.
details=[o for o in bpy.context.scene.objects if o.type=='MESH' and o!=body]
if details:
    bpy.ops.object.select_all(action='DESELECT')
    for o in details:o.select_set(True)
    bpy.context.view_layer.objects.active=details[0];bpy.ops.object.join()
    bpy.context.object.name='Starfish_SurfaceDetails'
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(P/'Starfish.gltf'),export_format='GLTF_SEPARATE',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'Starfish.glb'),export_format='GLB',use_selection=True)
scene=bpy.context.scene;target=Vector((0,0,.04))
bpy.ops.object.camera_add(location=(3,-4,6))
camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=3.9;scene.camera=camera
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.world.color=(.17,.19,.21)
for pos,power in [((-3,-4,6),550),((3,2,4),450)]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    o=bpy.context.object;o.data.energy=power;o.data.size=4
    o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=900;scene.render.resolution_y=800;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'Starfish.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'Starfish.blend'))
bpy.ops.render.render(write_still=True)
print('COMPLETE',len(spots),'surface details')

