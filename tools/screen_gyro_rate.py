"""Reproduce host-only gyro-rate experiments; never edits or flashes firmware."""
import argparse
import csv
import io
from pathlib import Path
import shutil
import subprocess

MAIN = r'''
int main(void) {
#if SCREEN_KIND == 2
 const float ps[]={.3f,.5f,.7f,.9f},is[]={6,8,10,12,16};
 const float ds[]={0,.005f,.01f,.02f,.04f,.08f},fs[]={0,.01f,.02f},caps[]={.5f,1,2,3};
#else
 const float ps[]={.7f},is[]={6};
 const float ds[]={.005f,.01f,.02f,.03f,.04f,.06f,.08f,.12f};
 const float fs[]={0,.005f,.01f,.02f,.04f},caps[]={1,2,3,6};
#endif
 const float ts[]={.01f,.02f,.04f,.08f},gs[]={1,1.1f,1.2f},steps[]={5,30};
 unsigned int p,i,d,f,k,a,b,cd,sd,j,away,checked;
 ControlConfig baseline=Active(0);
 Result ref=Run(baseline,10,.04f,1.1f,0,0,2);
 assert(ref.rms>0); /* Reference function also guards configuration/limits. */
 puts("p,ki,kd,filter_s,cap_deg,away_only,eligible,checked_cases,max_observed_overshoot_deg,max_observed_entry_ms,rms2_deg,rms4_deg");
 for(p=0;p<sizeof(ps)/sizeof(ps[0]);++p)
 for(i=0;i<sizeof(is)/sizeof(is[0]);++i)
 for(d=0;d<sizeof(ds)/sizeof(ds[0]);++d)
 for(f=0;f<sizeof(fs)/sizeof(fs[0]);++f)
 for(k=0;k<sizeof(caps)/sizeof(caps[0]);++k)
 for(away=0;away<(SCREEN_KIND==2?2u:1u);++away) {
  ControlConfig c=Config(1,ps[p],ds[d],fs[f],caps[k]);
  float over=0; unsigned int slow=0; int eligible=1;
  c.ki=is[i]; c.rate_away_only=(uint8_t)(SCREEN_KIND==1?1u:away); checked=0;
  for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<2;++j) {
   Result x=Run(c,steps[j],ts[a],gs[b],cd,sd,0); ++checked;
   if(x.over>over)over=x.over;
   if(x.entry>slow)slow=x.entry;
   if(over>2 || slow>=10000) { eligible=0; goto report; }
  }
 report:
  printf("%.3f,%.3f,%.3f,%.3f,%.3f,%u,%d,%u,%.6f,%u,",
    (double)c.kp,(double)c.ki,(double)c.kd,(double)c.rate_filter_seconds,
    (double)c.rate_term_limit_deg,c.rate_away_only,eligible,checked,(double)over,slow);
  if(eligible) {
   Result r2=Run(c,10,.04f,1.1f,0,0,2),r4=Run(c,10,.04f,1.1f,0,0,4);
   printf("%.6f,%.6f\n",(double)r2.rms,(double)r4.rms);
  } else puts(",");
 }
 return 0;
}
'''

WIDE_MAIN = r'''
int main(void) {
 const float ts[]={.06f,.12f,.24f,.4f},gs[]={.8f,1,1.2f},steps[]={-30,-5,5,30};
 unsigned int a,b,cd,sd,j,count=0,slow=0; float over=0;
 ControlConfig c=Active(1); Result r2,r4;
 for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=3;cd+=3)
 for(sd=0;sd<=6;sd+=3)for(j=0;j<4;++j) {
  Result x=Run(c,steps[j],ts[a],gs[b],cd,sd,0);
  if(x.over>over)over=x.over;
  if(x.entry>slow)slow=x.entry;
  ++count;
 }
 r2=Run(c,10,.04f,1.1f,0,0,2); r4=Run(c,10,.04f,1.1f,0,0,4);
 puts("p,ki,kd,filter_s,cap_deg,away_only,eligible,checked_cases,max_observed_overshoot_deg,max_observed_entry_ms,rms2_deg,rms4_deg");
 printf("%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%.6f,%u,%.6f,%.6f\n",
   (double)c.kp,(double)c.ki,(double)c.kd,(double)c.rate_filter_seconds,
   (double)c.rate_term_limit_deg,c.rate_away_only,(unsigned int)(over<=2&&slow<10000),
   count,(double)over,slow,(double)r2.rms,(double)r4.rms);
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=['symmetric', 'away', 'joint', 'wide'], default='joint')
    parser.add_argument('--compiler', default=shutil.which('gcc') or
                        'C:/win64devkit/w64devkit/bin/gcc.exe')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    relative = 'build/gyro_rate_' + args.kind
    out = root / relative
    out.mkdir(parents=True, exist_ok=True)
    source = (root / 'tests/control_rate_test.c').read_text(encoding='utf-8')
    if source.count('int main(void)') != 1:
        raise ValueError('Model harness changed; review script before running')
    code = ('#define SCREEN_KIND ' + str(['symmetric', 'away', 'joint', 'wide'].index(args.kind)) +
            '\n' + source.split('int main(void)', 1)[0] + (WIDE_MAIN if args.kind == 'wide' else MAIN))
    (out / 'screen.c').write_text(code, encoding='utf-8')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-IDrivers/Control', '-IApplication', 'Drivers/Control/control.c',
               relative + '/screen.c', '-lm', '-o', relative + '/screen.exe']
    subprocess.run(command, cwd=root, check=True)
    result = subprocess.run([str(out / 'screen.exe')], cwd=root, check=True,
                            text=True, capture_output=True)
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    expected = 1 if args.kind == 'wide' else (2880 if args.kind == 'joint' else 160)
    if len(rows) != expected:
        raise ValueError(f'Expected {expected} trials, received {len(rows)}')
    (out / 'results.csv').write_text(result.stdout, encoding='utf-8')
    passed = [row for row in rows if row['eligible'] == '1']
    print(f'{args.kind}: {len(rows)} trials; {len(passed)} passed the selected model gate')
    print(out / 'results.csv')
    if passed:
        best = min(passed, key=lambda r: float(r['rms2_deg']) + float(r['rms4_deg']) +
                   .0001 * int(r['max_observed_entry_ms']))
        print('Best screening score (not automatic release selection):', best)


if __name__ == '__main__':
    main()
