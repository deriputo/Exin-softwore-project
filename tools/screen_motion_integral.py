"""Host-only screening of growing-error integral boost; no hardware access."""
from pathlib import Path
import os
import subprocess
import csv
import io

MAIN = r'''
int main(void){
 const float ps[]={.7f,1,1.3f,1.6f,2,2.5f};
 const float boosts[]={8,10,12,16,24,32},scales[]={10,30,60,120,240,480};
 const float ts[]={.01f,.02f,.04f,.08f},gs[]={1,1.1f,1.2f},steps[]={-30,-5,5,30};
 unsigned int p,i,k,a,b,cd,sd,j;
 puts("kp,boost,scale,pass,cases,overshoot,entry,rms10p2,rms10p4,rms30p4");
 for(p=0;p<6;++p)for(i=0;i<6;++i)for(k=0;k<6;++k){
  ControlConfig c=Active(1);float over=0;unsigned int entry=0,count=0;int pass=1;
  c.kp=ps[p];c.motion_integral_ki=boosts[i];c.motion_growth_scale=scales[k];
  for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<4;++j){
   Result r=Run(c,steps[j],ts[a],gs[b],cd,sd,0);++count;
   if(r.over>over)over=r.over;
   if(r.entry>entry)entry=r.entry;
   if(over>2||entry>=10000){pass=0;goto report;}
  }
 report:
  printf("%.1f,%.1f,%.1f,%d,%u,%.6f,%u,%.6f,%.6f,%.6f\n",(double)c.kp,(double)boosts[i],(double)scales[k],pass,count,(double)over,entry,
   (double)Run(c,10,.04f,1.1f,0,0,2).rms,(double)Run(c,10,.04f,1.1f,0,0,4).rms,
   (double)Run(c,30,.04f,1.1f,0,0,4).rms);
 }
 return 0;
}
'''

def main():
    root = Path(__file__).resolve().parent.parent
    out = root / 'build/motion_integral'
    out.mkdir(parents=True, exist_ok=True)
    prefix = (root / 'tests/control_rate_test.c').read_text(encoding='utf-8').split('int main(void)', 1)[0]
    (out / 'screen.c').write_text(prefix + MAIN, encoding='utf-8')
    env = dict(os.environ, TMPDIR=str(out))
    subprocess.run(['C:/win64devkit/w64devkit/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-pedantic',
                    '-IDrivers/Control','-IApplication','Drivers/Control/control.c',
                    'build/motion_integral/screen.c','-lm','-o','build/motion_integral/screen.exe'],cwd=root,env=env,check=True)
    result = subprocess.run([str(out / 'screen.exe')],cwd=root,check=True,text=True,capture_output=True)
    (out / 'results.csv').write_text(result.stdout,encoding='utf-8')
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    passed = [r for r in rows if r['pass']=='1']
    print(f'{len(passed)}/{len(rows)} passed finite model overshoot gate')
    for row in sorted(passed,key=lambda r:float(r['rms10p2'])+float(r['rms10p4']))[:5]:
        print(row)
    print(out / 'results.csv')

if __name__ == '__main__':
    main()
