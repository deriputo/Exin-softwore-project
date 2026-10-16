"""Host-only real-controller comparison of smooth PID deadband widths."""
from pathlib import Path
import os
import subprocess

MAIN=r'''
int main(void){
 const float widths[]={0,.1f,.2f,.3f,.5f,1,2};
 const float ts[]={.01f,.02f,.04f,.08f},gs[]={1,1.1f,1.2f},steps[]={-30,-5,5,30};
 unsigned int w,a,b,cd,sd,j;
 puts("width,cases,overshoot,entry,fine_entry,tail_error,rms2,rms4,max_tracking_ratio");
 for(w=0;w<7;++w){
  ControlConfig c=Active(1),old=c;float over=0,ratio=0,tail=0;unsigned int entry=0,fine=0,count=0;
  old.deadband_transition_deg=0;
  c.deadband_transition_deg=widths[w];
  for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<4;++j){
   Result r=Run(c,steps[j],ts[a],gs[b],cd,sd,0);++count;
   if(r.over>over)over=r.over;
   if(r.entry>entry)entry=r.entry;
   if(r.fine_entry>fine)fine=r.fine_entry;
   if(r.tail_error>tail)tail=r.tail_error;
  }
  for(a=1;a<4;++a)for(sd=0;sd<=2;sd+=2)for(j=2;j<=4;j+=2){
   Result r=Run(c,10,ts[a],1.1f,0,sd,(int)j),before=Run(old,10,ts[a],1.1f,0,sd,(int)j);
   float q=r.rms/before.rms;if(q>ratio)ratio=q;
  }
  printf("%.3f,%u,%.6f,%u,%u,%.6f,%.6f,%.6f,%.6f\n",(double)widths[w],count,(double)over,entry,fine,(double)tail,
   (double)Run(c,10,.04f,1.1f,0,0,2).rms,(double)Run(c,10,.04f,1.1f,0,0,4).rms,(double)ratio);
 }
 return 0;
}
'''
def main():
 root=Path(__file__).resolve().parent.parent;out=root/'build/deadband_screen'
 out.mkdir(parents=True,exist_ok=True)
 prefix=(root/'tests/control_rate_test.c').read_text(encoding='utf-8').split('int main(void)',1)[0]
 (out/'screen.c').write_text(prefix+MAIN,encoding='utf-8')
 env=dict(os.environ,TMPDIR=str(out))
 subprocess.run(['C:/win64devkit/w64devkit/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-pedantic',
  '-IDrivers/Control','-IApplication','Drivers/Control/control.c','build/deadband_screen/screen.c',
  '-lm','-o','build/deadband_screen/screen.exe'],cwd=root,env=env,check=True)
 r=subprocess.run([str(out/'screen.exe')],cwd=root,text=True,capture_output=True,check=True)
 (out/'results.csv').write_text(r.stdout,encoding='utf-8');print(r.stdout)
if __name__=='__main__':main()
