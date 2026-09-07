"""Build a rigid-weight walk rig from the original shrimp. Run with Blender 4.4."""
import bpy
import math
from pathlib import Path
from mathutils import Vector

HERE = Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(HERE / 'shrimp.gltf'))

# Rear legs were exported as one object. Split the disconnected legs so each
# can have its own timing, without changing their rest shape or materials.
rear = bpy.data.objects.get('Bacl_Leg_ALL')
bpy.ops.object.select_all(action='DESELECT')
rear.select_set(True)
bpy.context.view_layer.objects.active = rear
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.00001)
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')
# Each rear leg has two disconnected pieces. Keep each pair on the same joint
# to prevent a gap at the knee during the swing.
rear_parts = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o.name.startswith('Bacl_Leg_ALL')]
def centroid(obj):
    return sum((obj.matrix_world @ v.co for v in obj.data.vertices), Vector()) / len(obj.data.vertices)
upper_parts = [o for o in rear_parts if centroid(o).z > -0.6]
lower_parts = [o for o in rear_parts if centroid(o).z <= -0.6]
assert len(upper_parts) == len(lower_parts) == 8
for upper_part in upper_parts:
    c = centroid(upper_part)
    candidates = [o for o in lower_parts if centroid(o).x * c.x > 0]
    lower_part = min(candidates, key=lambda o: (centroid(o)-c).length)
    lower_parts.remove(lower_part)
    bpy.ops.object.select_all(action='DESELECT')
    upper_part.select_set(True)
    lower_part.select_set(True)
    bpy.context.view_layer.objects.active = upper_part
    bpy.ops.object.join()
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
for obj in meshes:
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

arm = bpy.data.armatures.new('ShrimpWalkRig')
rig = bpy.data.objects.new('ShrimpWalkRig', arm)
bpy.context.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
root = arm.edit_bones.new('Root')
root.head, root.tail = (0, 0, 0), (0, 0, 0.3)
descriptors = []
rear_count = 0
for obj in meshes:
    points = [v.co.copy() for v in obj.data.vertices]
    center = sum(points, Vector()) / len(points)
    lower = min(p.z for p in points)
    upper = max(p.z for p in points)
    name = obj.name.lower()
    is_leg = 'leg' in name
    kind = 'leg' if is_leg else 'antenna' if ('beard' in name or 'beaed' in name) else 'tail' if name == 'tail' else 'body'
    if is_leg:
        tips = [p for p in points if p.z >= upper - (upper-lower)*0.08]
        pivot = sum(tips, Vector()) / len(tips)
        if name.startswith('bacl'):
            rear_count += 1
            obj.name = f'RearLeg_{rear_count:02}'
    else:
        pivot = center.copy()
        if kind == 'antenna':
            pivot.z = lower
        if kind == 'tail':
            pivot.y = max(p.y for p in points)
    bone = arm.edit_bones.new(obj.name + '_Joint')
    bone.head = pivot
    bone.tail = pivot + Vector((0, 0, 0.25))
    bone.parent = root
    descriptors.append((obj, bone.name, kind, center.x, center.y))
bpy.ops.object.mode_set(mode='OBJECT')
for obj, bone_name, *_ in descriptors:
    group = obj.vertex_groups.new(name=bone_name)
    group.add(list(range(len(obj.data.vertices))), 1.0, 'REPLACE')
    modifier = obj.modifiers.new('Walk Rig', 'ARMATURE')
    modifier.object = rig
    obj.parent = rig

scene = bpy.context.scene
scene.render.fps = 30
scene.frame_start, scene.frame_end = 1, 32
# Frame 33 repeats frame 1: exact seam for the 32-frame in-place walk.
for frame in range(1, 34):
    phase = (frame-1) / 32 * 2 * math.pi
    p = rig.pose.bones['Root']
    p.location = (0, 0, 0.018 * (1 - math.cos(phase*2)))
    p.keyframe_insert('location', frame=frame)
    for obj, bone_name, kind, x, y in descriptors:
        p = rig.pose.bones[bone_name]
        p.rotation_mode = 'XYZ'
        if kind == 'leg':
            wave = phase + (math.pi if x > 0 else 0) + abs(y)*1.2
            # Bone local X matches world X; local Y points vertically upward.
            p.rotation_euler = (0.23*math.sin(wave), 0.035*math.sin(wave), 0)
        elif kind == 'antenna':
            p.rotation_euler = (0.035*math.sin(phase + x*3), 0.025*math.sin(phase*2), 0)
        elif kind == 'tail':
            p.rotation_euler = (0.025*math.sin(phase-0.6), 0.018*math.sin(phase), 0)
        else:
            p.rotation_euler = (0, 0, 0)
        p.keyframe_insert('rotation_euler', frame=frame)
action = rig.animation_data.action
action.name = 'Shrimp_Walk'
for curve in action.fcurves:
    for key in curve.keyframe_points:
        key.interpolation = 'LINEAR'
    curve.modifiers.new('CYCLES')

# Export just the character; floor/camera are only for the editable preview.
scene.frame_end = 33
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
for obj in meshes:
    obj.select_set(True)
scene.frame_set(1)
bpy.ops.export_scene.gltf(filepath=str(HERE / 'shrimp_walk.glb'),
    export_format='GLB', use_selection=True, export_animations=True,
    export_animation_mode='ACTIONS', export_force_sampling=True)
bpy.ops.export_scene.gltf(filepath=str(HERE / 'shrimp_walk.gltf'),
    export_format='GLTF_SEPARATE', use_selection=True, export_animations=True,
    export_animation_mode='ACTIONS', export_force_sampling=True)
scene.frame_end = 32

positions = [obj.matrix_world @ v.co for obj in meshes for v in obj.data.vertices]
lo = Vector(tuple(min(p[i] for p in positions) for i in range(3)))
hi = Vector(tuple(max(p[i] for p in positions) for i in range(3)))
target = (lo+hi)*0.5
bpy.ops.object.camera_add(location=target + Vector((7, 8, 5)))
camera = bpy.context.object
camera.rotation_euler = (target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = max(hi-lo)*1.45
scene.camera = camera
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.background_type = 'WORLD'
scene.world.color = (0.045, 0.065, 0.085)
scene.render.resolution_x = 640
scene.render.resolution_y = 640
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(HERE / 'shrimp_walk.blend'))
preview = HERE / 'walk_preview'
preview.mkdir(exist_ok=True)
for frame in range(1, 33, 2):
    scene.frame_set(frame)
    scene.render.filepath = str(preview / f'{frame:02}.png')
    bpy.ops.render.render(write_still=True)
print('WALK_COMPLETE', len(meshes), 'meshes;', rear_count, 'rear leg parts;', len(arm.bones), 'bones')
