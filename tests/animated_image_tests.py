"""Real GIF/WebP timelines, compositing and rejection, inspected independently with Pillow."""
import argparse,pathlib,subprocess,tempfile,hashlib,struct
from PIL import Image,ImageDraw,ImageSequence
p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=pathlib.Path);a=p.parse_args();exe=a.exe.resolve()
root=pathlib.Path(tempfile.mkdtemp(prefix='animated-images-',dir=pathlib.Path.cwd()/'test-output'))
def convert(source,ext,ok=True,options=None):
 output=root/(source.stem+'-'+str(len(list(root.iterdir())))+'.'+ext);temp=output.with_suffix('.tmp')
 command=[str(exe),'--convert',str(source),str(output),str(temp)]
 if options:command+=['--options',str(options)]
 before=hashlib.sha256(source.read_bytes()).hexdigest()
 r=subprocess.run(command,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=60)
 assert before==hashlib.sha256(source.read_bytes()).hexdigest(),'source changed'
 error=pathlib.Path(str(temp)+'.error.txt')
 assert (r.returncode==0)==ok,(source,ext,error.read_text(encoding='utf-8') if error.exists() else r.stderr)
 if not ok:assert not output.exists();return
 assert output.exists() and not temp.exists();return output
def inspect(path):
 with Image.open(path) as im:
  loop=im.info.get('loop',None);frames=[];durations=[]
  for frame in ImageSequence.Iterator(im):
   frames.append(frame.convert('RGBA').copy());durations.append(frame.info.get('duration',0))
  # WebP updates frame duration on load(), not seek alone.
  if path.suffix=='.webp':
   durations=[]
   for i in range(im.n_frames):im.seek(i);im.load();durations.append(im.info['duration'])
  return frames,durations,loop
frames=[]
for x,color in [(2,'red'),(14,'blue'),(24,'green')]:
 im=Image.new('RGBA',(48,32),(0,0,0,0));ImageDraw.Draw(im).rectangle((x,5,x+12,20),fill=color);frames.append(im)
gif=root/'source.gif';frames[0].save(gif,save_all=True,append_images=frames[1:],duration=[70,230,410],loop=2,disposal=2,transparency=0,optimize=False)
webp=root/'source.webp';frames[0].save(webp,save_all=True,append_images=frames[1:],duration=[70,230,410],loop=3,lossless=True)
for source in [gif,webp]:
 original,delays,loop=inspect(source)
 for ext in ['gif','webp']:
  output=convert(source,ext);actual,times,count=inspect(output)
  assert len(actual)==len(original) and times==delays,(source,ext,times,delays)
  expected_loop=2 if ext=='gif' else 3
  assert count==expected_loop,(ext,count,expected_loop)
  for f,g in zip(original,actual):
   for xy in [(0,0),(7,10),(19,10),(29,10)]:
    x,y=f.getpixel(xy),g.getpixel(xy);assert x==y or (x[3]==y[3]==0),(source,ext,xy,x,y)
  print(source.suffix,'->',ext,'frames/time/loop/alpha PASS',flush=True)
 first=convert(source,'png');assert all(x==y or x[3]==y[3]==0 for x,y in zip(Image.open(first).convert('RGBA').get_flattened_data(),original[0].get_flattened_data())),'first frame'
 option=root/(source.stem+'resize.options');option.write_text('version=1\nimage_max_dimension=24\n')
 resized=convert(source,'webp',options=option);assert inspect(resized)[0][0].size==(24,16)
# One-shot, infinite loop and a single image.
for loop in [None,0]:
 f=root/('loop-'+str(loop)+'.gif');kwargs={} if loop is None else {'loop':loop}
 frames[0].save(f,save_all=True,append_images=frames[1:],duration=[70,230,410],disposal=2,transparency=0,**kwargs)
 out=convert(f,'webp');assert inspect(out)[2]==(1 if loop is None else 0)
 back=convert(out,'gif');assert inspect(back)[2]==loop
still=root/'still.png';frames[0].save(still);one=convert(still,'gif');assert len(inspect(one)[0])==1
# Partial alpha survives WebP and is thresholded for GIF.
semi=frames.copy();semi[0]=Image.new('RGBA',(48,32),(60,80,200,127));semi[1]=Image.new('RGBA',(48,32),(60,80,200,200))
partial=root/'partial.webp';semi[0].save(partial,save_all=True,append_images=semi[1:],duration=[50,100,150],loop=0,lossless=True)
assert inspect(convert(partial,'webp'))[0][0].getpixel((0,0))[3]==127
assert inspect(convert(partial,'gif'))[0][0].getpixel((0,0))[3]==0
# Restore-previous disposal is composited before re-encoding.
restore=root/'restore.gif';frames[0].save(restore,save_all=True,append_images=frames[1:],duration=[50,100,150],loop=0,disposal=[1,3,2],transparency=0)
actual=inspect(convert(restore,'webp'))[0]
for f,g in zip(inspect(restore)[0],actual):assert all(x==y or x[3]==y[3]==0 for x,y in zip(f.get_flattened_data(),g.get_flattened_data()))
# Millisecond WebP timings and GIF's centisecond precision.
fast=root/'fast.webp';frames[0].save(fast,save_all=True,append_images=frames[1:],duration=[1,2,3],loop=0,lossless=True)
assert inspect(convert(fast,'webp'))[1]==[1,2,3]
assert inspect(convert(fast,'gif'))[1]==[10,10,10]
zero=root/'zero.webp';data=bytearray(webp.read_bytes());pos=data.index(b'ANMF');data[pos+20:pos+23]=b'\0\0\0';zero.write_bytes(data)
assert inspect(convert(zero,'webp'))[1]==[100,230,410]
zero_gif=root/'zero.gif';frames[0].save(zero_gif,save_all=True,append_images=frames[1:],duration=[0,230,410],loop=0,disposal=2)
assert inspect(convert(zero_gif,'webp'))[1]==[100,230,410]
high=root/'high-loop.gif';frames[0].save(high,save_all=True,append_images=frames[1:],duration=[70,230,410],loop=65535,disposal=2);convert(high,'webp',False)
# Oversized canvas, truncation, excessive frames/duration fail without publishing output.
for source in [gif,webp]:
 bad=root/('truncated'+source.suffix);bad.write_bytes(source.read_bytes()[:-3]);convert(bad,'png',False)
bad=root/'large.gif';data=bytearray(gif.read_bytes());data[6:10]=struct.pack('<HH',32768,32768);bad.write_bytes(data);convert(bad,'webp',False)
bad=root/'long.webp';data=bytearray(webp.read_bytes());pos=data.index(b'ANMF');data[pos+20:pos+23]=(600001).to_bytes(3,'little');bad.write_bytes(data);convert(bad,'png',False)
many=root/'many.gif';tiny=[Image.new('RGB',(2,2),'red' if i%2 else 'blue') for i in range(1001)];tiny[0].save(many,save_all=True,append_images=tiny[1:],duration=10,loop=0);convert(many,'png',False)
total=root/'total.gif';tiny[0].save(total,save_all=True,append_images=tiny[1:6],duration=10);data=bytearray(total.read_bytes());data[6:10]=struct.pack('<HH',10000,4000);total.write_bytes(data);convert(total,'png',False)
bad=root/'bounds.webp';data=bytearray(webp.read_bytes());pos=data.index(b'ANMF');data[pos+8:pos+11]=(1000).to_bytes(3,'little');bad.write_bytes(data);convert(bad,'png',False)
print('PASS animated timelines, loop, alpha, resize, first frame, limits and source preservation')
