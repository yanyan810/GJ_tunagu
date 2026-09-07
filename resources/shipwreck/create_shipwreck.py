"""Split the user's ship into two nearby, grounded wreck sections."""
import bpy
import bmesh
import math
from pathlib import Path
from mathutils import Vector

P=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(P.parent/'Boss_Ship'/'sip.gltf'))
sources=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in sources:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)

# Retain the original ship's textures, with a subdued submerged/weathered tint.
for m in list(bpy.data.materials):
    if not m.use_nodes:continue
    bs=next((n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
    if not bs:continue
    bs.inputs['Roughness'].default_value=.9
    color=bs.inputs['Base Color']
    if color.is_linked:
        previous=color.links[0].from_socket
        mix=m.node_tree.nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY'
        mix.inputs[0].default_value=.60;mix.inputs[2].default_value=(.38,.53,.43,1)
        m.node_tree.links.new(previous,mix.inputs[1]);m.node_tree.links.new(mix.outputs[0],color)
    else:
        c=color.default_value;color.default_value=(c[0]*.66,c[1]*.73,c[2]*.68,c[3])

def material(name,col):
    m=bpy.data.materials.new(name);m.diffuse_color=(*col,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*col,1);bs.inputs['Roughness'].default_value=.95
    return m
steel=material('Exposed Dark Frames',(.11,.18,.17))
rust=material('Broken Rust Edges',(.34,.17,.075))
wood=material('Splintered Deck',(.30,.24,.13))
hull=material('Oxidized Hull',(.26,.105,.055))
deck=material('Weathered Grey Green Deck',(.24,.30,.26))
CUT=-1.10
def seam(y,z):return CUT+.10*math.sin(z*8)+.08*math.sin(y*9)
groups=[]
for sign,name,roll,yaw in [(-1,'Bow',-.12,-.09),(1,'Stern',.08,.07)]:
    root=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(root);groups.append(root)
    root.location=(CUT+sign*.52,sign*.10,0);root.rotation_euler=(roll,sign*.055,yaw)
    for source in sources:
        bm=bmesh.new();bm.from_mesh(source.data)
        for v in bm.verts:v.co.x-=seam(v.co.y,v.co.z)
        bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=.00001,
            plane_co=(0,0,0),plane_no=(1,0,0),clear_outer=sign<0,clear_inner=sign>0)
        if not bm.faces:bm.free();continue
        for v in bm.verts:v.co.x+=seam(v.co.y,v.co.z)-CUT
        data=bpy.data.meshes.new(name+'_'+source.name);bm.to_mesh(data);bm.free()
        obj=bpy.data.objects.new(name+'_'+source.name,data);bpy.context.collection.objects.link(obj);obj.parent=root
        for m in source.data.materials:data.materials.append(m)
        if source.name.startswith('MainShip'):
            data.materials.clear();data.materials.append(hull);data.materials.append(deck)
            for face in data.polygons:face.material_index=1 if face.normal.z>.45 or face.center.z>1.85 else 0
            # The source hull is assembled from individual surfaces. Thickness
            # gives the open fracture a readable edge without sealing the hull.
            solid=obj.modifiers.new('Hull plate thickness','SOLIDIFY');solid.thickness=.035
    bpy.context.view_layer.update()
for source in sources:bpy.data.objects.remove(source,do_unlink=True)

def beam(name,a,b,width,mat,root):
    a,b=Vector(a),Vector(b);d=b-a
    bpy.ops.mesh.primitive_cube_add(size=1,location=(a+b)*.5)
    obj=bpy.context.object;obj.name=name;obj.scale=(width,width,d.length)
    obj.rotation_euler=d.to_track_quat('Z','Y').to_euler();obj.data.materials.append(mat);obj.parent=root
    return obj
for index,root in enumerate(groups):
    s=-1 if index==0 else 1
    # Open transverse frames visible inside each broken half.
    for depth in [.22,.75,1.30]:
        x=s*depth
        beam('Exposed_floor_frame',(x,-1.20,.10),(x,1.20,.10),.075,steel,root)
        for side in [-1,1]:
            beam('Exposed_rib',(x,side*1.20,.10),(x,side*1.36,1.18),.075,steel,root)
    for y in [-1.28,-.8,-.3,.3,.8,1.28]:
        # Short irregular fragments project into the gap without bridging it.
        z=1.28 if abs(y)>.9 else .09
        beam('Torn_longitudinal_beam',(s*.55,y,z),(-s*(.10+.06*math.sin(y*8)),y+.045,z+.10*math.cos(y*7)),.065,rust,root)
    bpy.context.view_layer.update()
    minimum=min((o.matrix_world@v.co).z for o in root.children if o.type=='MESH' for v in o.data.vertices)
    root.location.z-=minimum
bpy.context.view_layer.update()

# A few nearby broken planks, kept close to the rupture.
for i in range(5):
    y=-1.9+i*.8
    o=beam('Loose_plank',(CUT-.35,y,.055),(CUT+.23,y+.18,.055),.06,wood,None)

asset=[o for o in bpy.context.scene.objects if o.type in ('MESH','EMPTY')]
bpy.ops.object.select_all(action='DESELECT')
for o in asset:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(P/'shipwreck.glb'),export_format='GLB',use_selection=True)
bpy.ops.export_scene.gltf(filepath=str(P/'shipwreck.gltf'),export_format='GLTF_SEPARATE',use_selection=True)

points=[o.matrix_world@v.co for o in asset if o.type=='MESH' for v in o.data.vertices]
lo=Vector([min(p[i] for p in points) for i in range(3)]);hi=Vector([max(p[i] for p in points) for i in range(3)])
center=(lo+hi)*.5;size=max(hi-lo)
scene=bpy.context.scene
bpy.ops.object.camera_add(location=center+Vector((-.6,-1.3,1.0))*size)
cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=size*1.3;scene.camera=cam
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.world.color=(.12,.18,.20)
for delta,power in [((-1,-1,2),90),((1,1,1),60)]:
    bpy.ops.object.light_add(type='AREA',location=center+Vector(delta)*size)
    o=bpy.context.object;o.data.energy=power*size*size;o.data.size=size
    o.rotation_euler=(center-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1200;scene.render.resolution_y=850;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(P/'shipwreck.png')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(P/'shipwreck.blend'))
bpy.ops.render.render(write_still=True)
print('COMPLETE',len(asset),'objects',flush=True)
