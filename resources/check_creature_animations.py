"""Validate exported loop tracks and encode rendered frames into GIFs."""
import json
import math
import struct
import shutil
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parent
entries=[('Archerfish','Archerfish','Swim'),('Halfbeak','halfbeak','Swim'),('suckfish','suckfish','Swim'),
 ('marlin','marlin','Swim'),('dolphin','dolphin','Swim'),('orca','orca','Swim'),('shark','shark','Swim'),
 ('pufferfish','pufferfish','Swim'),('crab','crab','Walk'),('mantis_shrimp','mantis_shrimp','Walk'),
 ('jellyfish','jellyfish','Pulse'),('Starfish','Starfish','Crawl'),('sea_urchin','sea_urchin','Idle'),('shell','shell','OpenClose')]
report=[]
for folder,name,clip in entries:
    p=ROOT/folder;g=json.loads((p/(name+'.gltf')).read_text())
    buffers=[(p/b['uri']).read_bytes() for b in g['buffers']]
    def read(index):
        a=g['accessors'][index];v=g['bufferViews'][a['bufferView']]
        fmt={5126:'f',5123:'H',5121:'B',5125:'I'}[a['componentType']]
        n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        width=struct.calcsize('<'+fmt*n);offset=v.get('byteOffset',0)+a.get('byteOffset',0)
        return [struct.unpack_from('<'+fmt*n,buffers[v['buffer']],offset+i*v.get('byteStride',width)) for i in range(a['count'])]
    assert g.get('skins'),name
    assert len(g['animations'])==1 and g['animations'][0]['name']==clip,name
    animated=False
    for sampler in g['animations'][0]['samplers']:
        values=read(sampler['output']);times=[t[0] for t in read(sampler['input'])]
        assert all(math.isfinite(x) for v in values for x in v),name
        assert all(a<b for a,b in zip(times,times[1:])),name
        assert abs(times[-1]-times[0]-1.6)<.001,(name,times[-1])
        assert max(abs(a-b) for a,b in zip(values[0],values[-1]))<1e-5,name
        animated|=any(max(abs(a-b) for a,b in zip(values[0],v))>.001 for v in values[1:-1])
    assert animated,name
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            assert 'JOINTS_0' in primitive['attributes'] and 'WEIGHTS_0' in primitive['attributes'],name
            assert all(abs(sum(w)-1)<1e-4 for w in read(primitive['attributes']['WEIGHTS_0'])),name
    frames=[Image.open(f).convert('RGB') for f in sorted((ROOT/'animation_frames'/name).glob('*.png'))]
    assert len(frames)==8,name
    frames[0].save(p/(name+'.gif'),save_all=True,append_images=frames[1:],duration=200,loop=0)
    if not (p/(name+'.png')).exists():shutil.copyfile(ROOT/'animation_frames'/name/'01.png',p/(name+'.png'))
    report.append({'model':name,'animation':clip,'duration_seconds':1.6,'loop_verified':True,'skin_weights_verified':True})
    print('PASS',name,clip)
(ROOT/'creature_animation_checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
