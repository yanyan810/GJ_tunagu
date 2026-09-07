import bpy
from pathlib import Path

ROOT=Path(__file__).resolve().parent
entries=[('Archerfish','Archerfish','Swim'),('Halfbeak','halfbeak','Swim'),('suckfish','suckfish','Swim'),
 ('marlin','marlin','Swim'),('dolphin','dolphin','Swim'),('orca','orca','Swim'),('shark','shark','Swim'),
 ('pufferfish','pufferfish','Swim'),('crab','crab','Walk'),('mantis_shrimp','mantis_shrimp','Walk'),
 ('jellyfish','jellyfish','Pulse'),('Starfish','Starfish','Crawl'),('sea_urchin','sea_urchin','Idle'),('shell','shell','OpenClose')]
for folder,name,clip in entries:
    p=ROOT/folder;bpy.ops.wm.open_mainfile(filepath=str(p/(name+'.blend')))
    rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    action=rig.animation_data.action
    for old in list(bpy.data.actions):
        if old!=action and old.users==0:bpy.data.actions.remove(old)
    action.name=clip
    # Pose-bone local Y is the model's up axis; exchange curve values rather
    # than the indices so the correction can be reviewed in the action editor.
    path='pose.bones["Root"].location'
    curves={fc.array_index:fc for fc in action.fcurves if fc.data_path==path}
    if clip in ('Swim','Walk'):
        for a,b in zip(curves[1].keyframe_points,curves[2].keyframe_points):a.co.y,b.co.y=b.co.y,a.co.y
    if clip=='Pulse':
        curves={fc.array_index:fc for fc in action.fcurves if fc.data_path=='pose.bones["Bell"].scale'}
        for a,b in zip(curves[1].keyframe_points,curves[2].keyframe_points):a.co.y,b.co.y=b.co.y,a.co.y
    scene=bpy.context.scene;scene.frame_set(1);scene.frame_end=49
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
    for o in scene.objects:
        if o.type=='MESH':o.select_set(True)
    for ext,fmt in [('glb','GLB'),('gltf','GLTF_SEPARATE')]:
        bpy.ops.export_scene.gltf(filepath=str(p/(name+'.'+ext)),export_format=fmt,use_selection=True,export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True)
    if name=='Archerfish':(p/'Archerfish_textured.gltf').write_text((p/'Archerfish.gltf').read_text(),encoding='utf-8')
    scene.frame_end=48;scene.render.filepath=str(p/(name+'.png'))
    bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(p/(name+'.blend')))
    if clip in ('Swim','Walk','Pulse'):
        for frame in range(1,49,6):
            scene.frame_set(frame);scene.render.filepath=str(ROOT/'animation_frames'/name/('%02d.png'%frame));bpy.ops.render.render(write_still=True)
    print('FINALIZED',name,clip,flush=True)
