"""Host-only proportional response study. Never accesses hardware or changes firmware."""
from pathlib import Path
import csv
import io
import os
import subprocess

MAIN = r'''
int main(void){
 const float ps[]={1,1.5f,2,3,4},ks[]={4,6,8,10,12,16};
 const float fs[]={.01f,.02f,.04f,.06f,.08f},ds[]={0,.005f,.01f};
 const float ts[]={.01f,.02f,.04f,.08f},gs[]={1,1.1f,1.2f},steps[]={-30,-5,5,30};
 unsigned int p,k,f,d,a,b,cd,sd,j;
 ControlConfig before=Active(1);Result refs[12];unsigned int n=0;
 trial_p_tau=0;
 for(a=1;a<4;++a)for(sd=0;sd<=2;sd+=2)for(j=2;j<=4;j+=2)
  refs[n++]=Run(before,10,ts[a],1.1f,0,sd,(int)j);
 puts("kp,ki,kd,p_filter,pass,cases,overshoot,entry,rms2,rms4,max_rms_ratio");
 for(p=0;p<5;++p)for(k=0;k<6;++k)for(f=0;f<5;++f)for(d=0;d<3;++d){
  ControlConfig c=before;float over=0,ratio=0;unsigned int entry=0,count=0;int pass=1;
  c.kp=ps[p];c.ki=ks[k];c.kd=ds[d];trial_p_tau=fs[f];
  for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<4;++j){
   Result r=Run(c,steps[j],ts[a],gs[b],cd,sd,0);++count;
   if(r.over>over)over=r.over;
   if(r.entry>entry)entry=r.entry;
   if(over>2||entry>=10000){pass=0;goto report;}
  }
  n=0;
  for(a=1;a<4;++a)for(sd=0;sd<=2;sd+=2)for(j=2;j<=4;j+=2){
   Result r=Run(c,10,ts[a],1.1f,0,sd,(int)j);float q=r.rms/refs[n++].rms;
   if(q>ratio)ratio=q;
  }
  if(ratio>=1)pass=0;
 report:
  printf("%.3f,%.3f,%.3f,%.3f,%d,%u,%.6f,%u,%.6f,%.6f,%.6f\n",
   (double)c.kp,(double)c.ki,(double)c.kd,(double)trial_p_tau,pass,count,(double)over,entry,
   (double)Run(c,10,.04f,1.1f,0,0,2).rms,(double)Run(c,10,.04f,1.1f,0,0,4).rms,(double)ratio);
 }
 return 0;
}
'''

def main():
    root=Path(__file__).resolve().parent.parent
    out=root/'build/proportional_response'
    out.mkdir(parents=True,exist_ok=True)
    source=(root/'Drivers/Control/control.c').read_text(encoding='utf-8')
    needle='    control->state.p_term = control->config.kp * error;'
    assert source.count(needle)==1
    # A temporary experiment uses the PID-unused FOLLOW storage. Production
    # firmware will receive a dedicated field only if a candidate is selected.
    source='extern float trial_p_tau;\n'+source.replace(needle,'''    control->follow_filtered_deg += dt / (trial_p_tau + dt) *
        (error - control->follow_filtered_deg);
    control->state.p_term = control->config.kp * control->follow_filtered_deg;''')
    (out/'control_trial.c').write_text(source,encoding='utf-8')
    prefix=(root/'tests/control_rate_test.c').read_text(encoding='utf-8').split('int main(void)',1)[0]
    (out/'screen.c').write_text('float trial_p_tau;\n'+prefix+MAIN,encoding='utf-8')
    env=dict(os.environ,TMPDIR=str(out))
    subprocess.run(['C:/win64devkit/w64devkit/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-pedantic',
        '-IDrivers/Control','-IApplication','build/proportional_response/control_trial.c',
        'build/proportional_response/screen.c','-lm','-o','build/proportional_response/screen.exe'],cwd=root,env=env,check=True)
    result=subprocess.run([str(out/'screen.exe')],cwd=root,check=True,text=True,capture_output=True)
    (out/'results.csv').write_text(result.stdout,encoding='utf-8')
    rows=list(csv.DictReader(io.StringIO(result.stdout)))
    passed=[r for r in rows if r['pass']=='1']
    print(f'{len(passed)}/{len(rows)} passed overshoot, settling and tracking checks')
    for r in sorted(passed,key=lambda r:float(r['max_rms_ratio']))[:8]:print(r)
    print(out/'results.csv')

if __name__=='__main__':main()
