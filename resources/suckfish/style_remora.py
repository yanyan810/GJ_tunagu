import bpy
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(P/'suckfish.gltf'))
body=bpy.data.objects['Cube']
disc=next(o for o in bpy.context.scene.objects if o.type=='MESH' and o!=body)
for o in [body,disc]:
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True)
    bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
body.name='Remora_Body';disc.name='Remora_SuctionDisc'
def mat(name,col,rough=.7):
    m=bpy.data.materials.new(name);m.diffuse_color=(*col,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*col,1);bs.inputs['Roughness'].default_value=rough
    return m
back=mat('Slate blue back',(.10,.20,.26))
side=mat('Blue grey flank',(.25,.39,.44))
belly=mat('Warm pale belly',(.65,.72,.69))
stripe=mat('Deep side stripe',(.045,.09,.12))
fin=mat('Fin slate',(.13,.26,.31))
edge=mat('Fin edge',(.26,.42,.45))
rim=mat('Disc rim',(.34,.47,.48))
groove=mat('Disc grooves',(.055,.12,.14))
ribmat=mat('Disc ridges',(.21,.35,.36))
eye=mat('Black eyes',(.008,.013,.016),.2)
iris=mat('Eye rim',(.43,.55,.53),.4)
glint=mat('Eye glint',(.92,.97,.96),.18)
body.data.materials.clear()
for m in [back,side,belly,stripe,fin]:body.data.materials.append(m)
for face in body.data.polygons:
    z=face.center.z
    face.material_index=4 if face.center.x>6.1 else 0 if z>.45 else 2 if z<-.4 else 3 if -.15<z<.18 else 1
disc.data.materials.clear();disc.data.materials.append(rim)
bvh=BVHTree.FromPolygons([v.co for v in body.data.vertices],[list(p.vertices) for p in body.data.polygons])
def surface(x,y,z,direction):
    hit=bvh.ray_cast(Vector((x,y,z)),Vector(direction))[0]
    return hit if hit is not None else Vector((x,0,0))
def mesh(name,verts,faces,materials):
    data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces);data.update()
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    for m in materials:data.materials.append(m)
    for p in data.polygons:p.material_index=p.index%len(materials)
    return obj
def finmesh(name,outline,thickness=.04):
    # A shallow closed wedge gives the fin visible thickness from either side.
    points=[Vector(p) for p in outline];n=len(points)
    normal=(points[1]-points[0]).cross(points[2]-points[0]).normalized()*thickness
    v=[tuple(p+normal) for p in points]+[tuple(p-normal) for p in points]
    faces=[tuple(range(n)),tuple(reversed(range(n,2*n)))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,faces,[fin,edge])
for s in [-1,1]:
    root=surface(-6.1,s*5,-.18,(0,-s,0));root.y-=s*.06
    finmesh('Pectoral_'+str(s),[root,root+Vector((2.7,s*2,-.55)),root+Vector((2.1,s*.30,-.10)),root+Vector((.8,0,0))])
    root=surface(-.4,s*5,-.35,(0,-s,0));root.y-=s*.05
    finmesh('Pelvic_'+str(s),[root,root+Vector((1.65,s*.85,-.45)),root+Vector((1.6,0,0))])
top1=surface(.0,0,5,(0,0,-1));top2=surface(5.5,0,5,(0,0,-1))
finmesh('Dorsal_Fin',[top1-Vector((0,0,.04)),top1+Vector((.7,0,.95)),top2+Vector((-.2,0,.4)),top2-Vector((0,0,.04))])
bottom1=surface(.7,0,-5,(0,0,1));bottom2=surface(5.3,0,-5,(0,0,1))
finmesh('Anal_Fin',[bottom1+Vector((0,0,.03)),bottom1+Vector((.8,0,-.65)),bottom2+Vector((0,0,-.25)),bottom2+Vector((0,0,.03))])
def sphere(name,location,scale,material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,location=location)
    o=bpy.context.object;o.name=name;o.scale=scale;o.data.materials.append(material)
    return o
for s in [-1,1]:
    top=surface(-9.65,0,5,(0,0,-1));bottom=surface(-9.65,0,-5,(0,0,1))
    height=(top.z+bottom.z)*.5
    pos=surface(-9.65,s*5,height,(0,-s,0))
    sphere('EyeSocket_'+str(s),pos,(.23,.10,.19),iris)
    sphere('Eye_'+str(s),pos+Vector((0,s*.075,0)),(.18,.10,.16),eye)
    sphere('EyeHighlight_'+str(s),pos+Vector((-.045,s*.16,.05)),(.04,.025,.04),glint)
    # A thin dark inset behind the eye reads as a gill slit.
    top=surface(-8.7,0,5,(0,0,-1));bottom=surface(-8.7,0,-5,(0,0,1))
    g=surface(-8.7,s*5,(top.z+bottom.z)*.5,(0,-s,0))
    sphere('Gill_'+str(s),g+Vector((0,s*.012,0)),(.045,.025,.16),stripe)

# Add paired lamellae on the existing suction disc, following its upper surface.
dbvh=BVHTree.FromPolygons([v.co for v in disc.data.vertices],[list(p.vertices) for p in disc.data.polygons])
for i in range(12):
    x=-9.85+i*.31
    hit=dbvh.ray_cast(Vector((x,0,4)),Vector((0,0,-1)))[0]
    if hit is None:continue
    for s in [-1,1]:
        finmesh('DiscLamella_%02d_%d'%(i,s),[(x-.055,s*.035,hit.z+.015),(x+.035,s*.43,hit.z+.03),(x+.13,s*.43,hit.z+.03),(x+.04,s*.035,hit.z+.06)],.018).data.materials[0]=ribmat
characters=[o for o in bpy.context.scene.objects if o.type=='MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in characters:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(P/'suckfish.glb'),export_format='GLB',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'suckfish.gltf'),export_format='GLTF_SEPARATE',use_selection=True)
scene=bpy.context.scene
center=Vector((-1,0,0))
bpy.ops.object.camera_add(location=(-13,-26,17))
camera=bpy.context.object;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=24
scene.camera=camera
scene.render.engine='CYCLES';scene.cycles.samples=32
scene.world.color=(.16,.19,.22)
for pos,power in [((-8,-10,18),4500),((5,10,12),3500)]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    o=bpy.context.object;o.data.energy=power;o.data.shape='DISK';o.data.size=12
    o.rotation_euler=(center-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1200;scene.render.resolution_y=700;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'suckfish.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'suckfish.blend'))
bpy.ops.render.render(write_still=True)
print('COMPLETE',len(characters),'mesh parts')

