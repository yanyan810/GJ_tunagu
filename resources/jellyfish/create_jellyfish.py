"""Create the low-poly jellyfish asset. Run with Blender 4.4 in background mode."""
import bpy
import math
from pathlib import Path
from mathutils import Vector

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
def material(name,color,roughness=.4):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value=(*color,1)
    bs.inputs['Roughness'].default_value=roughness
    return m
crown=material('Pearl Lavender',(.60,.51,.78))
bell=material('Soft Periwinkle',(.39,.57,.79))
rim=material('Aqua Rim',(.36,.72,.78))
inside=material('Bell Interior',(.32,.40,.66))
arm=material('Oral Arms Lavender',(.68,.49,.73))
armedge=material('Oral Arms Pearl',(.79,.69,.83))
tentacle=material('Tentacles Aqua',(.34,.67,.72))
tip=material('Tentacle Tips',(.58,.48,.77))
def mesh(name,vertices,faces,mats):
    data=bpy.data.meshes.new(name);data.from_pydata(vertices,[],faces);data.update()
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    for m in mats:data.materials.append(m)
    return obj

# A closed, thick bell with a recessed underside, rather than a solid sphere.
N=24
rings=[(.0,1.8),(.40,1.76),(.80,1.59),(1.10,1.30),(1.29,.95),
       (1.32,.72),(1.23,.69),(1.12,.91),(.80,1.19),(.40,1.36),(.0,1.4)]
verts=[]
for row,(r,z) in enumerate(rings):
    for j in range(N):
        a=2*math.pi*j/N
        scallop=.055*math.cos(a*12) if row in (4,5,6) else 0
        verts.append((r*math.cos(a),r*math.sin(a),z+scallop))
faces=[]
for i in range(len(rings)-1):
    for j in range(N):faces.append((i*N+j,i*N+(j+1)%N,(i+1)*N+(j+1)%N,(i+1)*N+j))
obj=mesh('Jellyfish_Bell',verts,faces,[crown,bell,rim,inside])
for f in obj.data.polygons:
    row=f.index//N
    f.material_index=0 if row<2 else 1 if row<4 else 2 if row<6 else 3

def tube(name,points,radii,mats):
    verts=[];sides=6
    for i,p in enumerate(points):
        p=Vector(p)
        tangent=Vector(points[min(i+1,len(points)-1)])-Vector(points[max(i-1,0)])
        tangent.normalize()
        axis=tangent.cross(Vector((0,1,0))).normalized()
        other=tangent.cross(axis).normalized()
        for j in range(sides):
            a=2*math.pi*j/sides
            verts.append(tuple(p+radii[i]*(axis*math.cos(a)+other*math.sin(a))))
    faces=[tuple(reversed(range(sides)))]
    for i in range(len(points)-1):
        for j in range(sides):faces.append((i*sides+j,i*sides+(j+1)%sides,(i+1)*sides+(j+1)%sides,(i+1)*sides+j))
    faces.append(tuple(range((len(points)-1)*sides,len(points)*sides)))
    obj=mesh(name,verts,faces,mats)
    for f in obj.data.polygons:
        f.material_index=1 if len(mats)>1 and f.index>len(faces)*.8 else 0
    return obj

# Thin peripheral tentacles vary in length and curve gently outward.
for j in range(12):
    a=2*math.pi*j/12
    length=2.05+.55*(.5+.5*math.sin(j*2.1))
    points=[];radii=[]
    for k in range(17):
        t=k/16
        r=1.19+.14*math.sin(t*5+j*.9)+.12*t
        drift=.13*math.sin(t*7+j)*t
        points.append((r*math.cos(a)+drift,r*math.sin(a)+.10*math.sin(t*6+j)*t,.73-length*t))
        radii.append(.041*(1-t)+.011)
    tube('Jellyfish_Tentacle_%02d'%(j+1),points,radii,[tentacle,tip])

# Four broader, folded oral arms underneath the center of the bell.
for j in range(4):
    angle=j*math.pi/2+math.pi/4
    vertices=[]
    for k in range(19):
        t=k/18
        width=(.18+.065*math.cos(t*math.pi*10))*(1-.7*t)
        radial=.28+.19*t+.10*math.sin(t*8+j)
        z=1.12-2.6*t
        center=Vector((radial*math.cos(angle),radial*math.sin(angle),z))
        side=Vector((math.cos(angle+t*3),math.sin(angle+t*3),.13*math.cos(t*24)))
        for layer in [-1,1]:
            for sign in [-1,1]:vertices.append(tuple(center+side*width*sign+Vector((0,.025*layer,0))))
    faces=[]
    for k in range(18):
        i=k*4;n=i+4
        faces.extend([(i,n,n+1,i+1),(i+2,i+3,n+3,n+2),(i,i+2,n+2,n),(i+1,n+1,n+3,i+3)])
    faces.extend([(0,1,3,2),(72,74,75,73)])
    obj=mesh('Jellyfish_OralArm_%02d'%(j+1),vertices,faces,[arm,armedge])
    for f in obj.data.polygons:f.material_index=(f.index//4)%2

characters=list(bpy.context.scene.objects)
# Recalculate all face normals, including the closed underside of the bell.
for obj in characters:
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(P/'jellyfish.glb'),export_format='GLB',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'jellyfish.gltf'),export_format='GLTF_SEPARATE',use_selection=True)

scene=bpy.context.scene
target=Vector((0,0,.0))
bpy.ops.object.camera_add(location=(5,-8,3.6))
camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=5.6;scene.camera=camera
scene.render.engine='CYCLES';scene.cycles.samples=32
scene.world.color=(.13,.17,.22)
for pos,power in [((2,-4,6),700),((-4,1,3),850)]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=5
    light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=800;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'jellyfish.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'jellyfish.blend'))
bpy.ops.render.render(write_still=True)
print('COMPLETE:',len(characters),'mesh objects')

