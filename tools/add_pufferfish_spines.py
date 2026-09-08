"""Add skinned, low-poly spines to the authored pufferfish without changing its source buffer."""
from pathlib import Path
import json, math, struct
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
folder = ROOT / 'resources/pufferfish'
p = folder / 'pufferfish.gltf'
g = json.loads(p.read_text(encoding='utf-8'))
assert not any(m.get('name') == 'PufferfishIvorySpines' for m in g['materials']), 'Already generated'
def read(i):
    a=g['accessors'][i]; v=g['bufferViews'][a['bufferView']]
    data=(folder/g['buffers'][v['buffer']]['uri']).read_bytes()
    dtype={5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
    width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    return np.ndarray((a['count'],width),dtype=dtype,buffer=data,
        offset=v.get('byteOffset',0)+a.get('byteOffset',0),
        strides=(v.get('byteStride',np.dtype(dtype).itemsize*width),np.dtype(dtype).itemsize)).copy()
body=g['meshes'][2]['primitives'][0]
verts=read(body['attributes']['POSITION']); faces=read(body['indices']).reshape(-1,3)
tri=verts[faces]; e1=tri[:,1]-tri[:,0]; e2=tri[:,2]-tri[:,0]
positions=[];normals=[];count=0
for i in range(160):
    y=1-2*(i+.5)/160; angle=i*math.pi*(3-math.sqrt(5)); r=math.sqrt(1-y*y)
    direction=np.array([r*math.cos(angle),y,r*math.sin(angle)])
    # Keep the face and the three fin roots free of spikes.
    if direction[2]>.60: continue
    if abs(direction[0])>.80 and abs(y)<.32: continue
    if direction[2]<-.86 and abs(y)<.35: continue
    h=np.cross(np.broadcast_to(direction,e2.shape),e2); det=np.einsum('ij,ij->i',e1,h)
    inv=np.divide(1.,det,out=np.zeros_like(det),where=abs(det)>1e-8)
    sv=-tri[:,0]; u=inv*np.einsum('ij,ij->i',sv,h); q=np.cross(sv,e1)
    v=inv*(q@direction); distance=inv*np.einsum('ij,ij->i',e2,q)
    valid=(abs(det)>1e-8)&(u>=0)&(v>=0)&(u+v<=1)&(distance>0)
    if not valid.any(): continue
    t=distance[valid].min(); surface=direction*t
    normal=np.array([surface[0],surface[1],surface[2]/(1.95**2)])
    normal/=np.linalg.norm(normal)
    length=.29+.10*((i*37)%11)/10
    radius=.070
    base=surface-normal*.045; tip=surface+normal*length
    tangent=np.cross(normal,[0,1,0] if abs(normal[1])<.9 else [1,0,0]);tangent/=np.linalg.norm(tangent)
    bitangent=np.cross(normal,tangent)
    ring=[base+radius*(math.cos(j*math.tau/6)*tangent+math.sin(j*math.tau/6)*bitangent) for j in range(6)]
    for j in range(6):
        a,b,c=ring[j],ring[(j+1)%6],tip
        n=np.cross(b-a,c-a);n/=np.linalg.norm(n)
        positions.extend([a,b,c]);normals.extend([n,n,n])
    count+=1
binary=bytearray(); buffer_index=len(g['buffers'])
def add(values,dtype,typ,component):
    while len(binary)%4: binary.append(0)
    arr=np.asarray(values,dtype=dtype); offset=len(binary);binary.extend(arr.tobytes())
    vi=len(g['bufferViews']);g['bufferViews'].append({'buffer':buffer_index,'byteOffset':offset,'byteLength':arr.nbytes,'target':34962})
    a={'bufferView':vi,'componentType':component,'count':len(arr),'type':typ}
    if typ=='VEC3':a.update(min=arr.min(axis=0).tolist(),max=arr.max(axis=0).tolist())
    ai=len(g['accessors']);g['accessors'].append(a);return ai
n=len(positions)
attrs={'POSITION':add(positions,'<f4','VEC3',5126),'NORMAL':add(normals,'<f4','VEC3',5126),
'TEXCOORD_0':add([[0,0]]*n,'<f4','VEC2',5126),'JOINTS_0':add([[0,0,0,0]]*n,'u1','VEC4',5121),
'WEIGHTS_0':add([[1,0,0,0]]*n,'<f4','VEC4',5126)}
mi=len(g['materials']);g['materials'].append({'name':'PufferfishIvorySpines','doubleSided':False,'pbrMetallicRoughness':{'baseColorFactor':[1.,.88,.57,1.],'metallicFactor':0,'roughnessFactor':.8}})
g['meshes'][2]['primitives'].append({'attributes':attrs,'material':mi,'mode':4})
g['buffers'].append({'uri':'pufferfish_spines.bin','byteLength':len(binary)})
(folder/'pufferfish_spines.bin').write_bytes(binary)
p.write_text(json.dumps(g,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'{count} spines, {n//3} triangles')
# A geometry preview to check attachment and clearance around eyes and fins.
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
fig=plt.figure(figsize=(12,6))
for k,az in enumerate([35,135]):
    ax=fig.add_subplot(1,2,k+1,projection='3d')
    ax.add_collection3d(Poly3DCollection(tri,facecolor='#d3ad65',edgecolor='none'))
    ax.add_collection3d(Poly3DCollection(np.array(positions).reshape(-1,3,3),facecolor='#ffe4a4',edgecolor='#9f7943',linewidth=.2))
    ax.set(xlim=(-2,2),ylim=(-2,2),zlim=(-2,2));ax.set_box_aspect((1,1,1));ax.view_init(elev=25,azim=az);ax.set_axis_off()
fig.savefig(ROOT.parent/'generated/pufferfish-spines-preview.png',dpi=150)
