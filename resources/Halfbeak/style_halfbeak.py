"""Color and refine the original halfbeak; original files stay untouched."""
import bpy
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(P/'halfbeak.gltf'))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
body=max(objects,key=lambda o:o.dimensions.x)
for o in objects:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
def mat(name,color,rough=.45,metal=.0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1)
    bs.inputs['Roughness'].default_value=rough;bs.inputs['Metallic'].default_value=metal
    return m
back=mat('Deep Teal Back',(.055,.24,.29))
silver=mat('Silver Blue Flank',(.48,.67,.70),.35,.15)
belly=mat('Pearl Belly',(.79,.84,.78))
fin=mat('Sea Glass Fins',(.26,.50,.52))
finrim=mat('Fin Highlights',(.55,.73,.69))
beak=mat('Lower Jaw Coral Tip',(.69,.22,.16))
black=mat('Glossy Black Eyes',(.004,.009,.012),.16)
eyeedge=mat('Silver Eye Ring',(.68,.76,.61),.3)
white=mat('Eye Catchlight',(.95,.99,1),.2)
gill=mat('Gill Shadow',(.055,.16,.18))
body.name='Halfbeak_Body'
body.data.materials.clear()
for m in [back,silver,belly,fin,beak]:body.data.materials.append(m)
for f in body.data.polygons:
    x,z=f.center.x,f.center.z
    f.material_index=4 if x < -3.82 else 3 if x>-.20 else 0 if f.normal.z>.35 else 2 if f.normal.z<-.35 else 1
eyes=[o for o in objects if o!=body and o.dimensions.x<.1]
fins=[o for o in objects if o!=body and o not in eyes]
def sphere(name,pos,scale,m):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,location=pos)
    o=bpy.context.object;o.name=name;o.scale=scale;o.data.materials.append(m)
    return o
for i,o in enumerate(eyes):
    center=sum((v.co for v in o.data.vertices),Vector())/len(o.data.vertices)
    s=1 if center.y>0 else -1
    o.name='Halfbeak_Eye_'+('Left' if s>0 else 'Right')
    o.data.materials.clear();o.data.materials.append(black)
    # A slightly wider socket sits behind the original eye mesh.
    sphere('Halfbeak_EyeRing_%d'%i,center-Vector((0,s*.014,0)),(.048,.031,.048),eyeedge)
    sphere('Halfbeak_EyeGlint_%d'%i,center+Vector((-.012,s*.035,.014)),(.010,.006,.010),white)
for i,o in enumerate(fins):
    o.name='Halfbeak_Fin_%02d'%(i+1);o.data.materials.clear()
    o.data.materials.append(fin);o.data.materials.append(finrim)
    for f in o.data.polygons:f.material_index=1 if f.normal.z>.5 else 0
bvh=BVHTree.FromPolygons([v.co for v in body.data.vertices],[list(f.vertices) for f in body.data.polygons])
for s in [-1,1]:
    hit=bvh.ray_cast(Vector((-2.96,s*2,.06)),Vector((0,-s,0)))[0]
    if hit is not None:
        sphere('Halfbeak_Gill_'+str(s),hit+Vector((0,s*.003,0)),(.012,.008,.065),gill)

characters=[o for o in bpy.context.scene.objects if o.type=='MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in characters:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(P/'halfbeak.glb'),export_format='GLB',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'halfbeak.gltf'),export_format='GLTF_SEPARATE',use_selection=True)
scene=bpy.context.scene;target=Vector((-1.75,0,0))
bpy.ops.object.camera_add(location=(-4,-8,3))
camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=5.5;scene.camera=camera
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.world.color=(.17,.20,.23)
for pos,power in [((-3,-4,5),500),((1,3,4),650)]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    o=bpy.context.object;o.data.energy=power;o.data.size=5
    o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1200;scene.render.resolution_y=650;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'halfbeak.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'halfbeak.blend'))
bpy.ops.render.render(write_still=True)
print('COMPLETE',len(characters),'mesh parts')

