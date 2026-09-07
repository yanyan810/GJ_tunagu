import bpy
from pathlib import Path
from mathutils import Vector

p = Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(p / 'Archerfish_textured.gltf'))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
points = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
lo = Vector([min(v[i] for v in points) for i in range(3)])
hi = Vector([max(v[i] for v in points) for i in range(3)])
center = (lo+hi)*0.5
size = max(hi-lo)
print('BOUNDS',lo,hi)
bpy.ops.object.camera_add(location=center+Vector((size*0.2,-size*1.8,size*0.4)))
cam=bpy.context.object
cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO'
cam.data.ortho_scale=size*1.3
scene=bpy.context.scene
scene.camera=cam
scene.render.engine='CYCLES'
scene.cycles.samples=24
scene.world.color=(0.25,0.25,0.25)
for delta,power in [((1,1,2),1300),((-1,-1,1),900)]:
    bpy.ops.object.light_add(type='AREA',location=center+Vector(delta)*size)
    light=bpy.context.object
    light.data.energy=power*size*size/16
    light.data.shape='DISK'
    light.data.size=size*1.5
    light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=960
scene.render.resolution_y=640
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.filepath=str(p/'Archerfish_preview.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(p/'Archerfish_textured.blend'))
bpy.ops.render.render(write_still=True)

