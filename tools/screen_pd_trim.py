"""Host-only P/D plus integral-trim experiment. Never connects to hardware.

Reuses the real PID branch (already the proposed fixed-bias P/I/D structure),
and the independent physical models in control_direct_test.c. Firmware is not
edited. Results are not hardware performance guarantees.
"""
from pathlib import Path
import argparse
import csv
import io
import shutil
import subprocess


MAIN = r'''
int main(void)
{
    const float ps[] = {1, 1.5f, 2, 3, 4, 6};
    const float is[] = {.2f, .4f, .8f, 1.2f, 2, 3};
    const float ds[] = {0, .02f, .04f, .06f, .10f, .16f, .24f, .4f, .6f};
    const float taus[] = {.06f, .12f, .24f, .4f};
    const float gains[] = {.8f, 1, 1.2f}, steps[] = {5, 30};
    unsigned int p, i, k, a, b, d, e, f;
    puts("kp,ki,kd,worst_overshoot_deg,worst_entry_ms,tail_failures,reference15_ms,tracking_rms_deg,eligible");
    for (p=0;p<6;++p) for (i=0;i<6;++i) for (k=0;k<9;++k) {
        float worst=0, tracking; uint32_t slow=0; unsigned int bad=0;
        Result reference; ControlConfig cfg;
        screen_p=ps[p]; screen_i=is[i]; screen_d=ds[k];
        for(a=0;a<4;++a) for(b=0;b<3;++b)
        for(d=0;d<=3;d+=3) for(e=0;e<=6;e+=3) for(f=0;f<2;++f) {
            Result r=Scenario(CONTROL_MODE_PID,steps[f],taus[a],gains[b],d,e);
            if(r.overshoot>worst) worst=r.overshoot;
            if(r.entry_ms>slow) slow=r.entry_ms;
            if(Abs(r.tail_error)>1) ++bad;
        }
        reference=Scenario(CONTROL_MODE_PID,15,.24f,1,0,0);
        cfg=Config(CONTROL_MODE_PID);
        tracking=Tracking(cfg,.12f,0,4);
        printf("%.3f,%.3f,%.3f,%.6f,%lu,%u,%lu,%.6f,%u\n",
            (double)screen_p,(double)screen_i,(double)screen_d,(double)worst,
            (unsigned long)slow,bad,(unsigned long)reference.entry_ms,
            (double)tracking,(unsigned int)(worst<=2 && slow<10000 && bad==0));
    }
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=shutil.which('gcc') or
                        'C:/win64devkit/w64devkit/bin/gcc.exe')
    parser.add_argument('--profile', choices=['wide', 'fast'], default='wide',
                        help='fast is a hypothesis envelope, not identified hardware limits')
    parser.add_argument('--gains', choices=['strong', 'moderate'], default='strong')
    parser.add_argument('--candidate', action='store_true',
                        help='Check P=.7 I=6 D=.005 against both disturbance directions')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    relative = 'build/pd_trim_' + args.profile + '_' + args.gains
    if args.candidate:
        relative += '_candidate'
    out = root / relative
    out.mkdir(parents=True, exist_ok=True)
    source = (root / 'tests/control_direct_test.c').read_text(encoding='utf-8')
    needle = '    return c;'
    if source.count(needle) != 1 or source.count('int main(void)') != 1:
        raise ValueError('Test harness changed; review experiment before rerunning')
    # No actuator position estimate contributes to the PID output. Constant Ki
    # and the original full integral range avoid an artificial reachability cap.
    source = source.replace(needle, '''    c.mode = CONTROL_MODE_PID;
    c.kp = screen_p; c.ki = screen_i; c.kd = screen_d;
    c.integral_error_scale_deg = 0;
    c.integral_min_ki = 0;
    c.integral_limit_deg = 180;
    return c;''')
    source = source.replace('Control_Init(&c, &cfg, 0);', '''Control_Init(&c, &cfg, 0);
    assert(c.config.mode == CONTROL_MODE_PID);
    assert(c.config.kp == screen_p && c.config.ki == screen_i && c.config.kd == screen_d);''')
    main_source = MAIN
    if args.profile == 'fast':
        main_source = main_source.replace('{.06f, .12f, .24f, .4f}', '{.01f, .02f, .04f, .08f}')
        main_source = main_source.replace('{.8f, 1, 1.2f}', '{1, 1.1f, 1.2f}')
        main_source = main_source.replace('d<=3;d+=3', 'd<=1;d+=1')
        main_source = main_source.replace('e<=6;e+=3', 'e<=2;e+=1')
    if args.gains == 'moderate':
        main_source = main_source.replace('{1, 1.5f, 2, 3, 4, 6}', '{.3f, .5f, .7f, 1, 1.3f, 1.6f}')
        main_source = main_source.replace('{.2f, .4f, .8f, 1.2f, 2, 3}', '{.8f, 1.5f, 3, 6, 12, 20}')
        main_source = main_source.replace('{0, .02f, .04f, .06f, .10f, .16f, .24f, .4f, .6f}',
                                          '{0, .005f, .01f, .02f, .04f, .06f, .1f, .2f, .4f}')
    if args.candidate:
        begin = main_source.index('    const float ps[]')
        end = main_source.index('    const float taus[]')
        main_source = (main_source[:begin] +
                       '    const float ps[]={.7f}, is[]={6}, ds[]={.005f};\n' + main_source[end:])
        main_source = main_source.replace('p<6', 'p<1').replace('i<6', 'i<1').replace('k<9', 'k<1')
        main_source = main_source.replace('steps[] = {5, 30}', 'steps[] = {-30, -5, 5, 30}')
        main_source = main_source.replace('f<2', 'f<4')
    source = ('static float screen_p, screen_i, screen_d;\n' +
              source.replace('int main(void)', 'int PreservedTests(void)') + main_source)
    (out / 'screen.c').write_text(source, encoding='utf-8')
    command = [args.compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
               '-pedantic', '-IDrivers/Control', '-IApplication',
               'Drivers/Control/control.c', relative + '/screen.c',
               '-lm', '-o', relative + '/screen.exe']
    subprocess.run(command, cwd=root, check=True)
    result = subprocess.run([str(out / 'screen.exe')], cwd=root, check=True,
                            text=True, capture_output=True)
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    expected = 1 if args.candidate else 324
    if len(rows) != expected:
        raise ValueError(f'Expected {expected} candidates, received {len(rows)}')
    path = out / 'results.csv'
    path.write_text(result.stdout, encoding='utf-8')
    passing = sum(row['eligible'] == '1' for row in rows)
    print(f'Candidates: {len(rows)}; satisfying all gates: {passing}')
    print(f'Results: {path}')
    print('No firmware files modified. No serial or debug probe access.')


if __name__ == '__main__':
    main()
