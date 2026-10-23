"""Reproducible local 0.6 comparison. Development dependencies only; no timing assertions."""
import hashlib,json,pathlib,statistics,subprocess,zipfile
from PIL import Image,ImageDraw
from docx import Document
from reportlab.pdfgen import canvas
from pypdf import PdfReader
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def run(args):return subprocess.run([str(a) for a in args],check=True,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW)
def main():
 work=ROOT/'build/benchmark09';work.mkdir(exist_ok=True)
 archive=ROOT/'out/ShiftWheel-0.6.0-win-x64.zip'
 if sha(archive)!='3b64710afc952b6c04d30f27b49a98af931925a93c6ad184fc9165300256a340':raise ValueError('baseline ZIP hash')
 baseline=ROOT/'build/Release/ShiftWheel-baseline-0.6.exe'
 with zipfile.ZipFile(archive) as z:
  name=next(n for n in z.namelist() if n.endswith('/ShiftWheel.exe'));baseline.write_bytes(z.read(name))
 if sha(baseline)!='4eb79ba353f5cafbbe05007f37a471391a10427b6e677e44f0b810fb163fd390':raise ValueError('baseline EXE hash')
 exe=ROOT/'build/Release/ShiftWheel.exe';runner=ROOT/'build/Release/benchmark_runner.exe'
 ffmpeg=ROOT/'tools/modules/ffmpeg/ffmpeg.exe'
 options=work/'options.txt';options.write_text('version=1\nquality=balanced\nresources=balanced\nvideo_acceleration=cpu\npdf_ocr=1\nocr_languages=eng\n',encoding='ascii')
 image=Image.new('RGB',(1600,1000),'#c9e3ef');draw=ImageDraw.Draw(image)
 for i in range(0,1600,20):draw.rectangle((i,i%700,i+10,900),fill=(i%255,90,160))
 image.save(work/'image.png')
 run([ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=1280x720:rate=30','-t','4','-c:v','libx264','-threads','2','-pix_fmt','yuv420p',work/'video.mp4'])
 run([ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i','sine=frequency=440:duration=10',work/'audio.wav'])
 document=Document();document.add_heading('ShiftWheel benchmark',0);document.add_paragraph('Identical source. Unicode: Привет, Bakı.');document.save(work/'document.docx')
 c=canvas.Canvas(str(work/'text.pdf'));c.drawString(72,720,'ShiftWheel deterministic text fixture');c.save()
 scan=Image.new('RGB',(1500,700),'white');d=ImageDraw.Draw(scan)
 from PIL import ImageFont
 font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',60);d.text((90,200),'SHIFTWHEEL OCR BENCHMARK 12345',fill='black',font=font);scan.save(work/'scan.pdf','PDF',resolution=144)
 cases=[('image.png','jpg'),('video.mp4','mp4'),('audio.wav','flac'),('document.docx','pdf'),('text.pdf','txt'),('scan.pdf','txt')]
 originals={name:sha(work/name) for name,_ in cases};rows=[]
 for index,(name,extension) in enumerate(cases):
  outputs={}
  for iteration in range(3):
   order=[('0.6.0',baseline),('0.9.0',exe)]
   if iteration%2:order.reverse()
   for version,program in order:
    tag=f'{index}-{version}-{iteration}';output=work/f'{tag}.{extension}';temp=work/f'{tag}.tmp'
    if output.exists():output.unlink()
    result=run([runner,program,'--convert',work/name,output,temp,'--options',options]);metrics=json.loads(result.stdout)
    metrics.update(case=name,version=version,iteration=iteration,output_bytes=output.stat().st_size,output_sha256=sha(output))
    rows.append(metrics);outputs.setdefault(version,[]).append(output)
  a,b=outputs['0.6.0'][0],outputs['0.9.0'][0]
  if extension=='pdf':
   ra,rb=PdfReader(a),PdfReader(b);assert len(ra.pages)==len(rb.pages) and [p.extract_text() for p in ra.pages]==[p.extract_text() for p in rb.pages]
  else:assert a.read_bytes()==b.read_bytes(),f'output changed: {name}'
  assert originals[name]==sha(work/name),'source changed'
  print(name,'verified',flush=True)
 (work/'results.json').write_text(json.dumps(dict(environment='Windows x64; same engine files; CPU video; balanced two threads; 3 alternating iterations; cache warm, short fixtures',inputs=originals,rows=rows),indent=2)+'\n')
 for name,_ in cases:
  print(name,[(version,round(statistics.median(r['wall_ms'] for r in rows if r['case']==name and r['version']==version),1)) for version in ['0.6.0','0.9.0']],flush=True)
if __name__=='__main__':main()
