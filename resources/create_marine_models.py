"""Create seven distinct low-poly marine models with Blender 4.4."""
import bpy
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
def material(name,color,rough=.5):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=rough
    return m
def mesh(name,verts,faces,mats):
    d=bpy.data.meshes.new(name);d.from_pydata(verts,[],faces);d.update()
    o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o)
    for m in mats:d.materials.append(m)
    return o
def ball(name,pos,scale,mat,segments=16,rings=8):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments,ring_count=rings,location=pos)
    o=bpy.context.object;o.name=name;o.scale=scale;o.data.materials.append(mat);return o
def rod(name,a,b,r1,r2,mat,sides=8):
    a,b=Vector(a),Vector(b);d=b-a
    bpy.ops.mesh.primitive_cone_add(vertices=sides,radius1=r1,radius2=r2,depth=d.length,location=(a+b)*.5)
    o=bpy.context.object;o.name=name;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();o.data.materials.append(mat);return o
def fin(name,points,mat,width=.045):
    p=[Vector(v) for v in points];n=len(p)
    normal=(p[1]-p[0]).cross(p[2]-p[0]).normalized()*width
    verts=[tuple(v+normal) for v in p]+[tuple(v-normal) for v in p]
    faces=[tuple(range(n)),tuple(reversed(range(n,n*2)))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,verts,faces,[mat])
def body_rings(name,sections,mats):
    verts=[];N=16
    for x,ry,rz,center in sections:
        for j in range(N):
            a=j*2*math.pi/N;verts.append((x,ry*math.cos(a),center+rz*math.sin(a)))
    faces=[tuple(reversed(range(N)))]
    for k in range(len(sections)-1):
        for j in range(N):faces.append((k*N+j,k*N+(j+1)%N,(k+1)*N+(j+1)%N,(k+1)*N+j))
    faces.append(tuple(range((len(sections)-1)*N,len(sections)*N)))
    o=mesh(name,verts,faces,mats)
    for f in o.data.polygons:
        f.material_index=min(len(mats)-1,2) if f.center.z<-.12 else 0 if f.center.z>.20 else min(len(mats)-1,1)
    return o
def eyes(x,y,z,r=.09):
    for s in [-1,1]:
        ball('Eye_'+str(s),(x,s*y,z),(r,r*.60,r),BLACK)
        ball('Eye_glint_'+str(s),(x-r*.25,s*(y+r*.5),z+r*.28),(r*.25,r*.15,r*.25),WHITE,12,6)
def fish(kind):
    if kind=='orca':
        dark=material('Orca Black',(.018,.026,.035));light=material('Orca White',(.85,.89,.87))
        sections=[(-2.05,.15,.25,0),(-1.85,.47,.52,0),(-1.2,.67,.75,0),(-.2,.70,.78,0),(.7,.48,.58,0),(1.5,.18,.23,0),(2.0,.08,.12,0)]
        body_rings('Orca_Body',sections,[dark,dark,light]);eyes(-1.63,.51,.14,.065)
        for s in [-1,1]:
            ball('White_eye_patch_'+str(s),(-1.22,s*.61,.28),(.31,.055,.17),light)
            fin('Flipper_'+str(s),[(-1.12,s*.45,-.20),(-.55,s*1.30,-.68),(.1,s*1.32,-.65),(-.2,s*.45,-.25)],dark,.06)
        fin('Tall_dorsal',[(-.45,0,.64),(-.2,0,2.05),(.20,0,1.37),(.60,0,.55)],dark,.07)
        for s in [-1,1]:fin('Fluke_'+str(s),[(1.6,0,.05),(2.1,s*1.2,.16),(2.5,s*1.12,.10),(2.2,0,-.04)],dark)
    else:
        dolphin=kind=='dolphin';marlin=kind=='marlin'
        dark=material('Ocean Back',(.07,.22,.35) if marlin else (.20,.35,.42) if dolphin else (.13,.25,.30))
        side=material('Silver Flank',(.29,.51,.62) if marlin else (.44,.59,.63))
        pale=material('Pale Belly',(.77,.83,.79));fmat=material('Fin Blue',(.11,.29,.38))
        sections=[(-1.9,.09,.15,0),(-1.65,.28,.36,0),(-1.2,.43,.52,0),(-.4,.49,.58,0),(.55,.36,.42,0),(1.35,.14,.20,0),(1.8,.07,.11,0)]
        if dolphin:
            sections[0]=(-1.9,.13,.22,.08)
            sections[1]=(-1.65,.32,.43,.06)
        body_rings(kind.title()+'_Body',sections,[dark,side,pale]);eyes(-1.47,.37,.13,.075)
        if dolphin:
            body_rings('Dolphin_Rostrum',[(-2.50,.045,.065,-.11),(-2.32,.12,.10,-.09),(-1.65,.19,.16,-.06)],[side,side,pale])
            fin('Dorsal',[(-.45,0,.45),(-.23,0,1.20),(.20,0,.92),(.5,0,.43)],dark)
        elif marlin:
            rod('Long_bill',(-1.75,0,.05),(-3.4,0,.06),.10,.005,dark)
            fin('Swept_dorsal',[(-1.20,0,.35),(-.94,0,1.40),(-.45,0,1.20),(.20,0,.57),(1.12,0,.23)],dark)
        else:
            fin('Shark_dorsal',[(-.5,0,.46),(-.05,0,1.39),(.4,0,.40)],dark,.065)
            fin('Second_dorsal',[(.95,0,.24),(1.19,0,.56),(1.48,0,.15)],dark,.035)
            for s in [-1,1]:
                for i in range(5):
                    ball('Gill_%d_%d'%(s,i),(-1.15+i*.115,s*(.43+i*.01),-.015),(.012,.014,.16),dark,8,6)
        for s in [-1,1]:
            fin('Pectoral_'+str(s),[(-.95,s*.34,-.13),(.12,s*1.3,-.45),(.35,s*.78,-.34),(-.33,s*.36,-.18)],fmat)
            if not dolphin:fin('Pelvic_'+str(s),[(.60,s*.23,-.19),(1.1,s*.55,-.49),(1.19,s*.15,-.16)],fmat,.025)
        if dolphin:
            for s in [-1,1]:fin('Tail_fluke_'+str(s),[(1.5,0,.03),(1.88,s*.92,.05),(2.20,s*.90,-.03),(2.08,0,-.04)],dark)
        else:
            fin('Tail_upper',[(1.48,0,.08),(2.10,0,1.12),(2.27,0,1.05),(1.98,0,-.02)],fmat)
            fin('Tail_lower',[(1.53,0,-.08),(2.12,0,-.87),(2.26,0,-.83),(1.98,0,.03)],fmat)
def crab():
    red=material('Crab Coral',(.73,.19,.10));top=material('Shell Highlights',(.91,.35,.16));cream=material('Claw Tips',(.94,.72,.43))
    ball('Crab_Carapace',(0,0,.35),(1.0,.79,.40),red,16,8)
    ball('Top_shell',(0,0,.51),(.82,.66,.28),top,16,6)
    for s in [-1,1]:
        for i in range(4):
            a=(-.35+i*.29,s*.64,.26);b=(-.8+i*.55,s*1.22,.18);c=(-1.05+i*.75,s*(1.5+(.15 if i in (1,2) else 0)),-.25)
            rod('Walking_leg_upper',a,b,.11,.075,red);rod('Walking_leg_tip',b,c,.075,.009,top)
        rod('Eye_stalk',(-.64,s*.30,.52),(-.87,s*.34,.85),.065,.045,red)
        ball('Crab_eye',(-.88,s*.34,.86),(.09,.09,.09),BLACK)
        rod('Claw_arm',(-.54,s*.65,.35),(-1.18,s*1.02,.39),.15,.13,red)
        ball('Claw_palm',(-1.55,s*1.12,.45),(.48,.27,.26),top)
        for branch in [-1,1]:
            fin('Claw_pincer',[(-1.85,s*1.12+branch*.21,.47),(-2.50,s*1.12+branch*.22,.49),(-2.70,s*1.12+branch*.04,.49),(-2.40,s*1.12+branch*.09,.48),(-1.85,s*1.12+branch*.08,.47)],cream,.075)
def mantis():
    green=material('Mantis Jade Shell',(.13,.45,.32));lime=material('Shell Ridges',(.48,.65,.24));orange=material('Raptorial Coral',(.79,.29,.12));pale=material('Leg Sand',(.74,.67,.41))
    ball('Thorax',(-.65,0,.25),(.78,.43,.40),green)
    for i in range(6):
        ball('Abdomen_segment_%d'%i,(.05+i*.29,0,.19-i*.035),(.23,.40-i*.035,.28-i*.018),green)
        ball('Segment_ridge_%d'%i,(.05+i*.29,0,.33-i*.035),(.075,.36-i*.032,.17),lime)
    for s in [-1,1]:
        for i in range(5):
            a=(-.72+i*.34,s*.30,.08);b=(-.52+i*.34,s*.64,-.1);c=(-.7+i*.34,s*.72,-.42)
            rod('Walking_leg',a,b,.045,.035,pale,6);rod('Foot',b,c,.035,.008,pale,6)
        rod('Eye_stalk',(-1.14,s*.23,.40),(-1.45,s*.34,.75),.075,.045,lime)
        ball('Mantis_eye',(-1.47,s*.35,.77),(.12,.13,.13),green)
        ball('Eye_band',(-1.55,s*.35,.78),(.045,.125,.06),BLACK)
        rod('Raptorial_upper',(-1.1,s*.35,.20),(-1.53,s*.73,.27),.13,.16,orange)
        rod('Folded_forearm',(-1.53,s*.73,.27),(-1.20,s*.69,-.04),.15,.10,lime)
        ball('Smashing_club',(-1.48,s*.66,-.04),(.23,.13,.14),orange)
        rod('Antenna',(-1.3,s*.2,.30),(-2.05,s*.50,.34),.021,.004,lime,6)
        fin('Tail_fan',[(1.40,0,.05),(1.99,s*.59,-.04),(2.14,s*.20,-.08),(1.73,0,.06)],orange,.035)
def urchin():
    purple=material('Urchin Plum',(.17,.055,.21));spine=material('Purple Spines',(.22,.10,.30));ends=material('Lilac Tips',(.45,.25,.53))
    ball('Urchin_Test',(0,0,0),(.72,.72,.61),purple,20,12)
    count=110;gold=math.pi*(3-math.sqrt(5))
    for i in range(count):
        z=1-2*(i+.5)/count;r=math.sqrt(1-z*z);a=i*gold
        d=Vector((r*math.cos(a),r*math.sin(a),z));start=Vector((d.x*.68,d.y*.68,d.z*.57))
        length=.44+.19*(.5+.5*math.sin(i*2.7));mid=start+d*length*.76;end=start+d*length
        rod('Spine_%03d'%i,start,mid,.048,.014,spine,5);rod('Tip_%03d'%i,mid,end,.014,.001,ends,5)
    # Combine the spines for efficient importing while retaining face materials.
    parts=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.name!='Urchin_Test']
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();bpy.context.object.name='Urchin_Spines'
def save(name):
    folder=ROOT/name;folder.mkdir(exist_ok=True)
    chars=[o for o in bpy.context.scene.objects if o.type=='MESH']
    for o in chars:
        bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    for fmt,ext in [('GLB','glb'),('GLTF_SEPARATE','gltf')]:
        bpy.ops.export_scene.gltf(filepath=str(folder/(name+'.'+ext)),export_format=fmt,use_selection=True)
    points=[o.matrix_world@v.co for o in chars for v in o.data.vertices]
    lo=Vector([min(p[i] for p in points) for i in range(3)]);hi=Vector([max(p[i] for p in points) for i in range(3)])
    target=(lo+hi)*.5;size=max(hi-lo)
    angle=Vector((-.6,-1.9,1.3)) if name in ('crab','mantis_shrimp','sea_urchin') else Vector((-.45,-2,.8))
    bpy.ops.object.camera_add(location=target+angle*size)
    camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type='ORTHO';camera.data.ortho_scale=size*(1.55 if name in ('crab','mantis_shrimp','sea_urchin') else 1.25)
    scene=bpy.context.scene;scene.camera=camera;scene.render.engine='CYCLES';scene.cycles.samples=24;scene.world.color=(.16,.19,.23)
    for delta,power in [((-1,-1,2),110),((1,1,1),100)]:
        bpy.ops.object.light_add(type='AREA',location=target+Vector(delta)*size)
        o=bpy.context.object;o.data.energy=power*size*size;o.data.size=size*1.4
        o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
    scene.render.resolution_x=1000;scene.render.resolution_y=760;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(folder/(name+'.png'))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(folder/(name+'.blend')))
    bpy.ops.render.render(write_still=True)
    print('ASSET_COMPLETE',name,flush=True)

names=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['marlin','dolphin','orca','crab','mantis_shrimp','shark','sea_urchin']
for name in names:
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    BLACK=material('Eye Black',(.006,.01,.013),.2);WHITE=material('Eye White',(.96,.98,1),.25)
    if name in ('marlin','dolphin','orca','shark'):fish(name)
    elif name=='crab':crab()
    elif name=='mantis_shrimp':mantis()
    elif name=='sea_urchin':urchin()
    else:raise ValueError(name)
    save(name)
