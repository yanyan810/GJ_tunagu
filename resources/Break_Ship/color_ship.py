"""Replace Break_Ship materials while preserving all geometry and transforms."""
import bpy
import math
from pathlib import Path
from mathutils import Vector

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(P/'Break_Ship.gltf'))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
def material(name,color):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.88
    return m
palette=[material(n,c) for n,c in [
    ('Faded Petrol Paint',(.115,.245,.235)),
    ('Oxide Rust',(.30,.115,.045)),
    ('Rust Highlights',(.43,.20,.085)),
    ('Aged Deck Timber',(.34,.29,.19)),
    ('Pale Weathered Cabin',(.50,.53,.43)),
    ('Dark Oxidized Metal',(.075,.115,.11)),
    ('Old Bronze',(.36,.27,.105)),
    ('Dark Sea Glass',(.065,.22,.235))]]
before=[(o.name,len(o.data.vertices),len(o.data.polygons),tuple(v for row in o.matrix_world for v in row)) for o in objects]
for obj in objects:
    # Unique mesh copies keep per-part assignments from leaking to instances.
    obj.data=obj.data.copy();obj.data.materials.clear()
    for m in palette:obj.data.materials.append(m)
    name=obj.name.lower()
    panel_center=obj.matrix_world@(sum((v.co for v in obj.data.vertices),Vector())/len(obj.data.vertices))
    normal_matrix=obj.matrix_world.to_3x3().inverted().transposed()
    for f in obj.data.polygons:
        pos=obj.matrix_world@f.center;n=(normal_matrix@f.normal).normalized()
        if name.startswith('mainship'):
            # Broad panel variation, with timber on upward facing deck surfaces.
            weather=math.sin(panel_center.x*1.6+panel_center.z*.8)+.6*math.cos(panel_center.y*3+panel_center.z*2)
            idx=3 if n.z>.55 else 1 if weather>.5 else 2 if weather>.2 else 0
        elif '_face_' in name:idx=4 if abs(n.z)<.6 else 3
        elif 'beam_canon' in name:idx=5
        elif 'ball' in name or '球' in name:idx=6
        elif '円柱' in name or 'pall' in name:idx=5
        elif max(obj.dimensions)>2.7:idx=3
        else:idx=6 if abs(n.z)<.5 else 5
        f.material_index=idx
after=[(o.name,len(o.data.vertices),len(o.data.polygons),tuple(v for row in o.matrix_world for v in row)) for o in objects]
assert before==after,'Geometry must remain unchanged'
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(P/'Break_Ship.glb'),export_format='GLB',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'Break_Ship.gltf'),export_format='GLTF_SEPARATE',use_selection=True)
points=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
lo=Vector([min(p[i] for p in points) for i in range(3)]);hi=Vector([max(p[i] for p in points) for i in range(3)])
target=(lo+hi)*.5;size=max(hi-lo)
bpy.ops.object.camera_add(location=target+Vector((.35,1.3,1.2))*size)
camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=size*1.35
scene=bpy.context.scene;scene.camera=camera;scene.render.engine='CYCLES';scene.cycles.samples=32
scene.world.color=(.14,.18,.20)
for d,power in [((-1,-1,2),65),((1,1,1),45)]:
    bpy.ops.object.light_add(type='AREA',location=target+Vector(d)*size)
    o=bpy.context.object;o.data.energy=power*size*size;o.data.size=size*1.3
    o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1200;scene.render.resolution_y=850;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'Break_Ship.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'Break_Ship.blend'))
bpy.ops.render.render(write_still=True)
print('PASS: materials replaced; geometry and transforms unchanged',len(objects),'parts')
