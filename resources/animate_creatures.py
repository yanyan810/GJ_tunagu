"""Add looping skeletal animations to the static marine assets (Blender 4.4)."""
import bpy
import math
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
ASSETS=[('Archerfish','Archerfish','Archerfish_textured','fish'),('Halfbeak','halfbeak','halfbeak','fish'),
 ('suckfish','suckfish','suckfish','fish'),('marlin','marlin','marlin','fish'),
 ('dolphin','dolphin','dolphin','cetacean'),('orca','orca','orca','cetacean'),
 ('shark','shark','shark','fish'),('pufferfish','pufferfish','pufferfish','puffer'),
 ('crab','crab','crab','crab'),('mantis_shrimp','mantis_shrimp','mantis_shrimp','mantis'),
 ('jellyfish','jellyfish','jellyfish','jelly'),('Starfish','Starfish','Starfish','star'),
 ('sea_urchin','sea_urchin','sea_urchin','urchin'),('shell','shell','shell','shell')]
def center(o):return sum((v.co for v in o.data.vertices),Vector())/len(o.data.vertices)
def build(folder,name,source,kind):
    p=ROOT/folder
    # Idempotent: never replace an animation already present in the source.
    original=json.loads((p/(source+'.gltf')).read_text(encoding='utf-8-sig'))
    if original.get('animations'):
        print('SKIPPED_EXISTING',name,flush=True);return
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(p/(source+'.gltf')))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    for o in meshes:
        bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    points=[v.co.copy() for o in meshes for v in o.data.vertices]
    lo=Vector([min(v[i] for v in points) for i in range(3)]);hi=Vector([max(v[i] for v in points) for i in range(3)])
    mid=(lo+hi)*.5;size=max(hi-lo)
    arm=bpy.data.armatures.new(name+'_Rig');rig=bpy.data.objects.new(name+'_Rig',arm);bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode='EDIT')
    controllers={}
    def bone(label,pivot,parent='Root',motion=None):
        b=arm.edit_bones.new(label);b.head=pivot;b.tail=Vector(pivot)+Vector((0,0,max(.05,size*.035)))
        if parent:b.parent=arm.edit_bones[parent]
        if motion:controllers[label]=motion
        return label
    bone('Root',(0,0,0),None)
    assignments={}
    if kind in ('fish','cetacean'):
        # Longitudinal skinning blends at the joints to bend a unified body,
        # while the head/eyes retain their original rigid shape.
        direction=-1 if name=='Archerfish' else 1
        low,high=sorted((lo.x*direction,hi.x*direction));span=high-low
        spine=[low+span*t for t in (.42,.62,.82)]
        parent='Root'
        for i,t in enumerate(spine):
            label='Spine_%d'%i
            bone(label,(t*direction,0,0),parent,('swim',i,kind=='cetacean'));parent=label
        def weights(v,o):
            t=v.x*direction
            if t<=spine[0]:return [('Root',1)]
            for i in range(2):
                if t<spine[i+1]:
                    blend=(t-spine[i])/(spine[i+1]-spine[i])
                    return [('Root' if i==0 else 'Spine_%d'%(i-1),1-blend),('Spine_%d'%i,blend)]
            blend=min(1,(t-spine[2])/max(.001,high-spine[2]))
            return [('Spine_1',1-blend),('Spine_2',blend)]
    elif kind=='puffer':
        for o in meshes:
            if o.name.startswith('\u7acb\u65b9\u4f53'):
                c=center(o);pivot=Vector((c.x*.65,c.y*.65,c.z))
                assignments[o.name]=bone('Fin_'+str(len(assignments)),pivot,motion=('fin',1 if c.x>0 else -1,c.y>1))
        def weights(v,o):return [(assignments.get(o.name,'Root'),1)]
    elif kind in ('crab','mantis'):
        uppers=[o for o in meshes if o.name.startswith('Walking_leg_upper' if kind=='crab' else 'Walking_leg')]
        lowers=[o for o in meshes if o.name.startswith('Walking_leg_tip' if kind=='crab' else 'Foot')]
        available=list(lowers)
        for i,o in enumerate(uppers):
            # Match each lower segment to the closest upper endpoint, preserving
            # the original knee connection with a shared rigid leg controller.
            pts=[v.co for v in o.data.vertices];pivot=min(pts,key=lambda v:abs(v.y))
            label=bone('Leg_%02d'%i,pivot,motion=('leg',i,1 if center(o).y>0 else -1))
            assignments[o.name]=label
            if available:
                lower=min(available,key=lambda a:(center(a)-center(o)).length);available.remove(lower);assignments[lower.name]=label
        for side in [-1,1]:
            label=bone('Claw_'+str(side),(-.54,side*.65,.35) if kind=='crab' else (-1.1,side*.35,.20),motion=('claw',side))
            for o in meshes:
                tokens=('Claw','Raptorial','Folded_forearm','Smashing_club')
                if o.name.startswith(tokens) and center(o).y*side>0:assignments[o.name]=label
        def weights(v,o):return [(assignments.get(o.name,'Root'),1)]
    elif kind=='jelly':
        bone('Bell',(0,0,.72),motion=('bell',))
        for o in meshes:
            if 'Bell' in o.name:assignments[o.name]='Bell';continue
            pivot=max((v.co for v in o.data.vertices),key=lambda v:v.z)
            label=bone('Tendril_%02d'%len(assignments),pivot,'Bell',('tendril',len(assignments)))
            assignments[o.name]=label
        def weights(v,o):
            label=assignments[o.name]
            if label=='Bell':return [('Bell',1)]
            top=max(vv.co.z for vv in o.data.vertices)
            w=min(1,max(0,(top-v.z)/.65))
            return [('Bell',1-w),(label,w)]
    elif kind=='star':
        # Identify the five arm axes from the longest outline vertices.
        axes=[]
        for v in sorted(points,key=lambda v:v.x*v.x+v.y*v.y,reverse=True):
            a=math.atan2(v.y,v.x)
            if all(abs(math.atan2(math.sin(a-b),math.cos(a-b)))>.65 for b in axes):axes.append(a)
            if len(axes)==5:break
        for i,a in enumerate(axes):bone('Arm_%d'%i,(math.cos(a)*.2,math.sin(a)*.2,0),motion=('star',i,a))
        def weights(v,o):
            a=math.atan2(v.y,v.x);i=min(range(len(axes)),key=lambda i:abs(math.atan2(math.sin(a-axes[i]),math.cos(a-axes[i]))))
            w=min(1,max(0,(math.hypot(v.x,v.y)-.22)/.5))
            return [('Root',1-w),('Arm_%d'%i,w)]
    elif kind=='shell':
        for s in [-1,1]:bone('Valve_'+str(s),(mid.x,hi.y,0),motion=('shell',s))
        def weights(v,o):return [('Valve_'+str(1 if v.z>=0 else -1),1)]
    else:
        bone('Sway',(0,0,0),motion=('urchin',))
        def weights(v,o):return [('Sway',1)]
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in meshes:
        groups={b.name:o.vertex_groups.new(name=b.name) for b in arm.bones}
        for v in o.data.vertices:
            for label,w in weights(v.co,o):
                if w>0:groups[label].add([v.index],w,'REPLACE')
        mod=o.modifiers.new('Creature Animation','ARMATURE');mod.object=rig;o.parent=rig
    scene=bpy.context.scene;scene.render.fps=30;scene.frame_start=1;scene.frame_end=49
    for frame in range(1,50):
        phase=(frame-1)/48*2*math.pi
        for pb in rig.pose.bones:
            pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
        root=rig.pose.bones['Root']
        if kind in ('fish','cetacean','puffer'):root.location.y=.007*size*math.sin(phase)
        elif kind in ('crab','mantis'):root.location.y=.015*(1-math.cos(phase*2))
        for label,c in controllers.items():
            pb=rig.pose.bones[label];mode=c[0]
            if mode=='swim':
                angle=(.055+c[1]*.04)*math.sin(phase-c[1]*.6)
                if c[2]:pb.rotation_euler.z=angle
                else:pb.rotation_euler.y=angle
            elif mode=='fin':pb.rotation_euler.y=.25*c[1]*math.sin(phase*(1 if c[2] else 2))
            elif mode=='leg':
                wave=phase+c[1]*.6+(math.pi if c[2]<0 else 0)
                pb.rotation_euler.x=.13*c[2]*math.sin(wave);pb.rotation_euler.y=.10*math.cos(wave)
            elif mode=='claw':pb.rotation_euler.y=.06*c[1]*math.sin(phase-.5)
            elif mode=='bell':pb.scale=(1+.075*math.sin(phase),1-.10*math.sin(phase),1+.075*math.sin(phase))
            elif mode=='tendril':
                pb.rotation_euler.x=.06*math.sin(phase-c[1]*.3);pb.rotation_euler.z=.07*math.sin(phase-c[1]*.3-.7)
            elif mode=='star':
                angle=.065*math.sin(phase-c[1]*.8);pb.rotation_euler.x=angle*math.sin(c[2]);pb.rotation_euler.z=angle*math.cos(c[2])
            elif mode=='shell':pb.rotation_euler.x=c[1]*.085*(1-math.cos(phase))
            elif mode=='urchin':pb.rotation_euler.x=.018*math.sin(phase);pb.rotation_euler.z=.018*math.sin(phase)
        for pb in rig.pose.bones:
            pb.keyframe_insert('location',frame=frame);pb.keyframe_insert('rotation_euler',frame=frame);pb.keyframe_insert('scale',frame=frame)
    action=rig.animation_data.action
    action.name={'fish':'Swim','cetacean':'Swim','puffer':'Swim','crab':'Walk','mantis':'Walk','jelly':'Pulse','star':'Crawl','shell':'OpenClose','urchin':'Idle'}[kind]
    for fc in action.fcurves:
        for key in fc.keyframe_points:key.interpolation='LINEAR'
        fc.modifiers.new('CYCLES')
    scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
    for o in meshes:o.select_set(True)
    for ext,fmt in [('glb','GLB'),('gltf','GLTF_SEPARATE')]:
        bpy.ops.export_scene.gltf(filepath=str(p/(name+'.'+ext)),export_format=fmt,use_selection=True,export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True)
    if source!=name:
        # Keep the earlier textured entry point pointing at the same animated model.
        (p/(source+'.gltf')).write_text((p/(name+'.gltf')).read_text(),encoding='utf-8')
    target=mid;direction=Vector((-.4,-1.7,.9))
    if kind in ('crab','mantis','star','urchin','shell'):direction=Vector((-.6,-1.4,1.5))
    if kind=='puffer':direction=Vector((.9,-1.6,.6))
    bpy.ops.object.camera_add(location=target+direction*size)
    camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=size*1.65;scene.camera=camera
    scene.render.engine='CYCLES';scene.cycles.samples=8;scene.world.color=(.16,.19,.22)
    for d,power in [((-1,-1,2),65),((1,1,1),45)]:
        bpy.ops.object.light_add(type='AREA',location=target+Vector(d)*size)
        light=bpy.context.object;light.data.energy=power*size*size;light.data.size=size*1.4;light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
    scene.frame_end=48;scene.render.resolution_x=480;scene.render.resolution_y=400;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(p/(name+'.png'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(p/(name+'.blend')))
    frames=ROOT/'animation_frames'/name;frames.mkdir(parents=True,exist_ok=True)
    for frame in range(1,49,6):
        scene.frame_set(frame);scene.render.filepath=str(frames/('%02d.png'%frame));bpy.ops.render.render(write_still=True)
    print('ANIMATED',name,action.name,len(arm.bones),'bones',flush=True)

selected=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
for args in ASSETS:
    if not selected or args[1] in selected:build(*args)
