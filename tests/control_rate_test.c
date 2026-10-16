#include "app_config.h"
#include "control.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
static float Abs(float x){return x<0?-x:x;}
static ControlConfig Config(int rate,float p,float kd,float filter,float cap){
 ControlConfig c;Control_DefaultConfig(&c);c.mode=CONTROL_MODE_PID;c.axis=CONTROL_AXIS_PITCH;c.direction=-1;
 c.target_min_deg=0;c.target_limit_deg=180;c.integral_limit_deg=180;c.kp=p;c.ki=6;c.kd=kd;
 c.deadband_deg=.5f;c.slew_deg_per_second=180;c.use_rate_sample=(uint8_t)rate;
 c.rate_filter_seconds=filter;c.rate_term_limit_deg=cap;c.rate_quiet_dps=3;
 return c;
}
typedef struct{float over,rms;unsigned int entry,fine_entry;float tail_error;} Result;
static Result Run(ControlConfig cfg,float amplitude,float tau,float gain,unsigned int cd,unsigned int sd,int wave){
 Control c;ControlSample s={0,90,0,1,0,1};float commands[64],ys[64],rates[64],shaft=90,y=90,prev=90,sum=0;
 unsigned int n,index,last=1000,fine_last=1000,count=0;Result r={0};
 for(n=0;n<64;++n){commands[n]=ys[n]=90;rates[n]=0;}
 Control_Init(&c,&cfg,0);assert(c.config.use_rate_sample==cfg.use_rate_sample && c.config.kp==cfg.kp && c.config.ki==cfg.ki && c.config.kd==cfg.kd);assert(Control_StartHold(&c,&s,0)==CONTROL_OK);
 for(n=20;n<=20000;n+=20){
  float x=n<1000?0:(float)(n-1000)/1000;
  float base=n<1000?0:(wave?amplitude*sinf(6.2831853f*x/(float)wave):amplitude*(x<.1f?x/.1f:1));
  float old_y=y,cross;
  index=(n/20)%64;commands[index]=c.state.output_angle_deg;
  shaft+=.02f/(tau+.02f)*(commands[(index+64-cd)%64]-shaft);
  y=90+base-gain*(shaft-90);ys[index]=y;rates[index]=(y-old_y)/.02f;
  s.pitch_deg=ys[(index+64-sd)%64];s.angular_rate_dps=rates[(index+64-sd)%64];s.timestamp_ms=n;
  (void)Control_Update(&c,&s,n);assert(c.state.fault==CONTROL_FAULT_NONE);
  assert(c.state.output_angle_deg>=0&&c.state.output_angle_deg<=180);
  assert(Abs(c.state.output_angle_deg-prev)<=3.601f);prev=c.state.output_angle_deg;
  if(n>=1000){cross=amplitude>0?90-y:y-90;if(cross>r.over)r.over=cross;if(Abs(y-90)>1)last=n;if(Abs(y-90)>.6f)fine_last=n;}
  if(n>=4000){sum+=(y-90)*(y-90);++count;}
 }
 r.rms=sqrtf(sum/count);r.entry=last<19000?last+20-1000:20000;
 r.fine_entry=fine_last<19000?fine_last+20-1000:20000;r.tail_error=Abs(y-90);return r;
}

static ControlConfig Active(int rate){
 ControlConfig c=Config(rate,APP_POSITION_PID_KP,rate?APP_RATE_PID_KD:APP_POSITION_PID_KD,
                       rate?APP_RATE_FILTER_SECONDS:.08f,rate?APP_RATE_TERM_LIMIT_DEG:0);
 c.ki=rate?APP_RATE_PID_KI:APP_POSITION_PID_KI;
 c.rate_quiet_dps=APP_RATE_QUIET_DPS;
 c.deadband_transition_deg=rate?APP_HOLD_DEADBAND_TRANSITION_DEG:0;return c;
}
int main(void){
 const float ts[]={.01f,.02f,.04f,.08f},gs[]={1,1.1f,1.2f},steps[]={-30,-5,5,30};
 ControlConfig cfg=Active(1),old=Active(0);Control c;ControlSample s={0,90,0,1,0,1};
 unsigned int a,b,cd,sd,j,count=0,slow=0,old_slow=0;float over=0,old_over=0;uint16_t held;
 for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<4;++j){
  Result x=Run(cfg,steps[j],ts[a],gs[b],cd,sd,0);
  Result before=Run(old,steps[j],ts[a],gs[b],cd,sd,0);
  if(before.over>old_over)old_over=before.over;
  if(before.entry>old_slow)old_slow=before.entry;
  assert(x.over<=2&&x.entry<10000);if(x.over>over)over=x.over;if(x.entry>slow)slow=x.entry;++count;
 }
 printf("RATE finite 100ms ramp: %u cases, overshoot=%.3fdeg entry=%ums\n",count,(double)over,slow);
 printf("Same ramp baseline: overshoot=%.3fdeg entry=%ums\n",(double)old_over,old_slow);
 {
  ControlConfig hard=cfg;unsigned int hard_entry=0,hard_fine=0,soft_fine=0;
  hard.deadband_transition_deg=0;
  for(a=0;a<4;++a)for(b=0;b<3;++b)for(cd=0;cd<=1;++cd)for(sd=0;sd<=2;++sd)for(j=0;j<4;++j){
   Result x=Run(cfg,steps[j],ts[a],gs[b],cd,sd,0),h=Run(hard,steps[j],ts[a],gs[b],cd,sd,0);
   if(h.entry>hard_entry)hard_entry=h.entry;
   if(h.fine_entry>hard_fine)hard_fine=h.fine_entry;
   if(x.fine_entry>soft_fine)soft_fine=x.fine_entry;
   assert(x.tail_error<=.501f);
  }
  assert(slow<=1000&&soft_fine<=hard_fine+100);
  printf("SOFT vs same-gain hard deadband: worst +/-1 entry %u -> %ums; +/-0.6 entry %u -> %ums\n",hard_entry,slow,hard_fine,soft_fine);
  for(a=1;a<4;++a)for(sd=0;sd<=2;sd+=2)for(j=2;j<=4;j+=2){
   Result x=Run(cfg,10,ts[a],1.1f,0,sd,(int)j),h=Run(hard,10,ts[a],1.1f,0,sd,(int)j);
   assert(x.rms<=h.rms*1.01f);
  }
 }
 for(a=1;a<4;++a)for(sd=0;sd<=2;sd+=2)for(j=2;j<=4;j+=2){
  Result before=Run(old,10,ts[a],1.1f,0,sd,(int)j),after=Run(cfg,10,ts[a],1.1f,0,sd,(int)j);
  assert(after.rms<before.rms);
  printf("Continuous tau=%.2f delay=%u period=%u: RMS %.4f -> %.4fdeg\n",(double)ts[a],sd*20,j,(double)before.rms,(double)after.rms);
 }
 Control_Init(&c,&cfg,0);assert(Control_StartHold(&c,&s,0)==CONTROL_OK);
 s.timestamp_ms=20;s.angular_rate_dps=100;(void)Control_Update(&c,&s,20);
 assert(c.state.p_term==0&&c.state.i_term==0&&c.state.d_term==-cfg.rate_term_limit_deg);
 assert(c.state.output_angle_deg>90); /* Motion response while attitude is still in the deadband. */
 s.timestamp_ms=40;s.angular_rate_dps=-400;(void)Control_Update(&c,&s,40);
 assert(c.state.d_term==cfg.rate_term_limit_deg&&c.state.output_angle_deg<90);
 s.angular_rate_dps=.5f;
 for(a=60;a<=60000;a+=20){
  s.timestamp_ms=a;s.pitch_deg=90+(a%40?.4f:-.4f);(void)Control_Update(&c,&s,a);
  assert(c.state.pulse_us==1500&&c.state.i_term==0&&c.state.d_term==0);
 }
 s.timestamp_ms=a;s.angular_rate_dps=0;s.pitch_deg=90;
 assert(Control_SetTarget(&c,100)==CONTROL_OK);(void)Control_Update(&c,&s,a);
 assert(c.state.d_term==0);held=c.state.pulse_us;
 s.timestamp_ms+=20;s.angular_rate_valid=0;
 assert(Control_Update(&c,&s,s.timestamp_ms)==held&&c.state.fault==CONTROL_FAULT_IMU_INVALID);
 s.angular_rate_valid=1;s.timestamp_ms+=20;
 assert(Control_Update(&c,&s,s.timestamp_ms)==held); /* Never silently fall back or rearm. */
 Control_StopHold(&c);assert(Control_StartHold(&c,&s,s.timestamp_ms)==CONTROL_OK);
 s.timestamp_ms+=20;s.angular_rate_dps=NAN;held=c.state.pulse_us;
 assert(Control_Update(&c,&s,s.timestamp_ms)==held&&c.state.fault==CONTROL_FAULT_IMU_INVALID);
 Control_Init(&c,&cfg,0);s.timestamp_ms=0;s.angular_rate_dps=0;s.pitch_deg=90;
 assert(Control_StartHold(&c,&s,0)==CONTROL_OK);
 s.angular_rate_dps=100;
 assert(Control_Update(&c,&s,101)==1500&&c.state.fault==CONTROL_FAULT_IMU_STALE);
 cfg.rate_away_only=1;Control_Init(&c,&cfg,0);s.pitch_deg=90;s.timestamp_ms=0;s.angular_rate_dps=0;
 assert(Control_StartHold(&c,&s,0)==CONTROL_OK&&Control_SetTarget(&c,100)==CONTROL_OK);
 s.timestamp_ms=20;s.angular_rate_dps=100;(void)Control_Update(&c,&s,20);assert(c.state.d_term==0);
 s.timestamp_ms=40;s.angular_rate_dps=-100;(void)Control_Update(&c,&s,40);assert(c.state.d_term>0);
 puts("PASS: RATE input, onset in deadband, bounded response, quiet noise, target kick, missing/NaN rate, latch and optional away gate");
 /* Experimental scheduling stays disabled in the application. Verify its
    increment/sign behavior independently before preserving the experiment. */
 cfg=Active(1);cfg.motion_integral_ki=10;cfg.motion_growth_scale=10;
 Control_Init(&c,&cfg,0);assert(c.config.motion_integral_ki==10);
 s.pitch_deg=90;s.timestamp_ms=0;s.angular_rate_dps=0;s.angular_rate_valid=1;
 assert(Control_StartHold(&c,&s,0)==CONTROL_OK);
 s.pitch_deg=91;s.angular_rate_dps=10;s.timestamp_ms=20;
 (void)Control_Update(&c,&s,20);
 assert(Abs(c.state.i_term+.18f)<.0001f); /* Ki9, moving away; old I not rescaled. */
 s.angular_rate_dps=-10;s.timestamp_ms=40;(void)Control_Update(&c,&s,40);
 assert(Abs(c.state.i_term+.34f)<.0001f); /* Return uses base Ki8. */
 s.pitch_deg=90.1f;s.angular_rate_dps=10;s.timestamp_ms=60;(void)Control_Update(&c,&s,60);
 assert(Abs(c.state.i_term+.34f)<.0001f); /* Deadband cannot accumulate boost. */
 cfg.motion_integral_ki=7;Control_Init(&c,&cfg,0);
 assert(c.config.motion_integral_ki==0); /* Reject boost below base gain. */
 cfg.motion_integral_ki=10;cfg.motion_growth_scale=NAN;Control_Init(&c,&cfg,0);
 assert(c.config.motion_integral_ki==0);
 puts("PASS: disabled experimental motion integral, growing/returning/deadband and config guards");
 return 0;
}
