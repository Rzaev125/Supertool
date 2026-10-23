"""Measure the current local build; a second local EXE is an optional baseline."""
import argparse,hashlib,json,pathlib,statistics,subprocess,tempfile
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
def execute(work,exe,baseline,iterations):
 runner=ROOT/'build/Release/benchmark_runner.exe'
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
  for iteration in range(iterations):
   order=([('baseline',baseline)] if baseline else [])+[('current',exe)]
   if iteration%2:order.reverse()
   for version,program in order:
    tag=f'{index}-{version}-{iteration}';output=work/f'{tag}.{extension}';temp=work/f'{tag}.tmp'
    if output.exists():output.unlink()
    result=run([runner,program,'--convert',work/name,output,temp,'--options',options]);metrics=json.loads(result.stdout)
    metrics.update(case=name,version=version,iteration=iteration,output_bytes=output.stat().st_size,output_sha256=sha(output))
    assert not temp.exists(),'temporary conversion output left behind'
    rows.append(metrics);outputs.setdefault(version,[]).append(output)
  if baseline:
   a,b=outputs['baseline'][0],outputs['current'][0]
   if extension=='pdf':
    ra,rb=PdfReader(a),PdfReader(b);assert len(ra.pages)==len(rb.pages) and [p.extract_text() for p in ra.pages]==[p.extract_text() for p in rb.pages]
   else:assert a.read_bytes()==b.read_bytes(),f'output changed: {name}'
  assert originals[name]==sha(work/name),'source changed'
  print(name,'verified',flush=True)
 (ROOT/'build/benchmark-results.json').write_text(json.dumps(dict(environment='Windows x64; CPU video; balanced two threads; synthetic short fixtures; no performance guarantees',iterations=iterations,executables={k:str(v) for k,v in ([('baseline',baseline)] if baseline else [])+[('current',exe)]},inputs=originals,rows=rows),indent=2)+'\n')
 for name,_ in cases:
  print(name,[(version,round(statistics.median(r['wall_ms'] for r in rows if r['case']==name and r['version']==version),1)) for version in (['baseline'] if baseline else [])+['current']],flush=True)

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/Release/ShiftWheel.exe')
 parser.add_argument('--baseline',type=pathlib.Path,help='Optional local EXE; must have its own matching modules beside it.')
 parser.add_argument('--iterations',type=int,choices=range(1,11),default=3)
 args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='benchmark-',dir=ROOT/'build') as directory:
  execute(pathlib.Path(directory),args.exe.resolve(),args.baseline.resolve() if args.baseline else None,args.iterations)
if __name__=='__main__':main()
