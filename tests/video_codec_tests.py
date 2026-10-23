"""Real HEVC/AV1 output through the app and the production fallback harness."""
import argparse,hashlib,json,re,subprocess,tempfile,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8');sys.stderr.reconfigure(encoding='utf-8')

p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--harness',type=Path,required=True);a=p.parse_args()
exe=a.exe.resolve();harness=a.harness.resolve();ffmpeg=exe.parent/'modules/ffmpeg/ffmpeg.exe';ffprobe=ffmpeg.with_name('ffprobe.exe')
def run(args,ok=True):
 r=subprocess.run(list(map(str,args)),capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
 assert (r.returncode==0)==ok,(args,r.stderr.decode('utf-8',errors='replace'))
 return r
def digest(p):return hashlib.sha256(p.read_bytes()).digest()
with tempfile.TemporaryDirectory(prefix='video-codecs-',dir=Path.cwd()/'test-output') as temporary:
 root=Path(temporary);source=root/'Исходник ə.mkv'
 run([ffmpeg,'-v','error','-f','lavfi','-i','testsrc2=size=322x242:rate=12:duration=1','-f','lavfi','-i','sine=frequency=700:sample_rate=48000:duration=1','-c:v','ffv1','-c:a','pcm_s16le','-threads','1',source])
 original=digest(source);number=0
 def convert(codec,ext,quality='balanced',acceleration='cpu',mode=None,ok=True):
  global number
  number+=1;stem=root/str(number);options=stem.with_suffix('.options');out=stem.with_suffix('.'+ext);temp=stem.with_suffix('.tmp')
  options.write_text(f'version=1\nvideo_codec={codec}\nquality={quality}\nvideo_acceleration={acceleration}\nresources=economy\nvideo_max_height=240\n')
  if mode:
   work=root/(str(number)+'-work');r=run([harness,'--real-media',source,out,work,options,mode],ok)
   trace=(work/'calls.tsv').read_text(encoding='utf-8') if (work/'calls.tsv').exists() else ''
  else:
   r=run([exe,'--convert',source,out,temp,'--options',options],ok);trace=r.stdout.decode('utf-8')
  assert digest(source)==original and not temp.exists()
  if not ok:assert not out.exists();return None,trace
  data=json.loads(run([ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',out]).stdout)
  video=next(s for s in data['streams'] if s['codec_type']=='video');audio=next(s for s in data['streams'] if s['codec_type']=='audio')
  assert video['codec_name']==codec and (video['width'],video['height'])==(320,240),video
  assert video['pix_fmt']=='yuv420p' and int(video['nb_read_frames'])==12,video
  assert abs(float(data['format']['duration'])-1)<0.2,data['format']
  assert audio['codec_name']==('opus' if ext=='webm' else 'aac')
  if ext in ['mp4','mov']:assert video['codec_tag_string']==('hvc1' if codec=='hevc' else 'av01')
  run([ffmpeg,'-v','error','-xerror','-err_detect','explode','-i',out,'-map','0:v:0','-map','0:a:0','-f','null','-'])
  return out,trace
 for codec,containers in [('hevc',['mp4','mkv','mov']),('av1',['mp4','mkv','webm'])]:
  for quality in ['compact','balanced','high']:
   for ext in containers:convert(codec,ext,quality)
  # Initialization failure exercises actual software encoding of the same codec.
  _,trace=convert(codec,'mp4',acceleration='auto',mode='unavailable')
  assert trace.count('probe\t')==3 and ('encode\t'+('libx265' if codec=='hevc' else 'libaom-av1')) in trace
  # Real local device initialization/full output; lack of modern AV1 GPU is valid.
  output,trace=convert(codec,'mp4',acceleration='auto',mode='auto')
  print(codec,'actual Auto trace:',trace.strip().replace('\n','; '),flush=True)
  for quality in ['compact','high']:convert(codec,'mp4',quality,acceleration='auto',mode='auto')
  _,fallback=convert(codec,'mp4',acceleration='auto',mode='runtime-failure')
  if re.search(r'encode\t'+codec+r'_(nvenc|qsv|amf)',fallback):
   assert ('encode\t'+('libx265' if codec=='hevc' else 'libaom-av1')) in fallback
  # Device-specific success is reported from this run, never inferred from names.
  _,intel=convert(codec,'mkv',acceleration='auto',mode='intel')
  print(codec,'Intel-isolated trace:',intel.strip().replace('\n','; '),flush=True)
  for quality in ['compact','high']:convert(codec,'mkv',quality,acceleration='auto',mode='intel')
  # All frames checked perceptually against the original scaled reference.
  result=run([ffmpeg,'-hide_banner','-i',source,'-i',output,'-filter_complex','[0:v]scale=320:240,setsar=1,setpts=PTS-STARTPTS[ref];[1:v]setsar=1,setpts=PTS-STARTPTS[enc];[ref][enc]psnr','-an','-f','null','-'])
  metric=re.search(rb'average:([0-9.]+)',result.stderr);assert metric and float(metric[1])>25,result.stderr
  print(codec,'all containers/presets, frame count, full decode, audio, scaling and PSNR PASS',flush=True)
 # No incompatible output can be published, even when Auto is selected.
 for codec,ext in [('hevc','webm'),('av1','mov'),('h264','webm')]:
  number+=1;options=root/f'{number}.options';out=root/f'{number}.{ext}';temp=root/f'{number}.tmp'
  options.write_text(f'version=1\nvideo_codec={codec}\n')
  run([exe,'--convert',source,out,temp,'--options',options],False)
  assert not out.exists() and not temp.exists()
  assert 'контейнер' in Path(str(temp)+'.error.txt').read_text(encoding='utf-8')
 assert digest(source)==original
print('PASS real HEVC/AV1 conversions and same-codec CPU fallback; originals preserved')
