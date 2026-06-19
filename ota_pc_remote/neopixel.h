#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "pins.h"

// Data pin comes from pins.h (NEOPIXEL_PIN, GPIO 19)
#define NUM_PIXELS 22

// Render at most this often. The animation is time-based (uses real elapsed
// seconds), so capping the frame rate does NOT change its speed - it only
// limits how often strip.show() runs. strip.show() disables interrupts for
// ~0.66ms on 22 LEDs, so an uncapped rate would starve WiFi/web handling.
#define NEO_FRAME_US 10000   // ~100 FPS

Adafruit_NeoPixel strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ===== BLOOM - calm base =====
const float BL_PERIOD=2.60, BL_EXPAND=1.20, BL_FADE=4.00;
const float BL_SIG0=1.30, BL_SIGMAX=8.00, BL_APEAK=0.90, BL_AFILL=0.40;
const float BL_LIFE=BL_EXPAND+BL_FADE;
const float W_BLOOM=0.55;

// ===== BAND - fluid character =====
const float BD_SPEED=0.55, BD_DRIFT=0.85, BD_MIN=1.20, BD_MAX=6.50;
const float BD_SNAP=0.55, BD_BIAS=0.55, BD_FR=3.0, BD_FL=4.0, BD_PHL=1.7;
const float BD_WOB=0.25, BD_EDGE=1.30;
const float W_BAND=0.60;

// ===== LASER - sparse accent =====
const float LS_PERIOD=5.50, LS_CHARGE=0.50, LS_TRAVEL=1.00, LS_FADE=1.20;
const float LS_HBCH=1.10, LS_HBLN=1.00, LS_HBMIN=0.55, LS_FILL=0.42;
const float LS_HEADW=1.40, LS_GATE=1.50, LS_BOOM=0.55, LS_BOOMD=12.0;
const float LS_FLASH=0.40, LS_FLSH=3.0, LS_VIB=0.14, LS_VF1=34.0, LS_VF2=57.0;
const float LS_LIFE=LS_CHARGE+LS_TRAVEL+LS_FADE;
const float W_LASER=1.00;

// ===== look controls =====
const float SHADE_SPEED=0.15;   // slow hue drift (rainbow as shade variation)
const float CONTRAST   =1.15;   // 1.0 = neutral, higher = snappier
const float MASTER     =0.85;   // overall brightness

unsigned long neoLastMicros;
float neoT=0;

float sstep(float e0,float e1,float x){float a=(x-e0)/(e1-e0);a=a<0?0:(a>1?1:a);return a*a*(3.0-2.0*a);}
float screenf(float a,float b){return a+b-a*b;}
float snapB(float s){return (s<0?-1.0:1.0)*powf(fabs(s),BD_SNAP);}
float crv(float v){float s=v*v*(3.0-2.0*v);return v+(s-v)*(CONTRAST-1.0);}

float bandI(float x,float tg){
  float ph=tg*BD_SPEED, span=(NUM_PIXELS-1)*0.5;
  float center=span+sinf(ph)*span*BD_DRIFT;
  float uR=powf(snapB(sinf(ph*BD_FR))*0.5+0.5,BD_BIAS);
  float uL=powf(snapB(sinf(ph*BD_FL+BD_PHL))*0.5+0.5,BD_BIAS);
  float extR=BD_MIN+(BD_MAX-BD_MIN)*uR, extL=BD_MIN+(BD_MAX-BD_MIN)*uL;
  float velR=fabs(cosf(ph*BD_FR)), velL=fabs(cosf(ph*BD_FL+BD_PHL));
  extR+=BD_WOB*velR*sinf(ph*BD_FR*3.0); extL+=BD_WOB*velL*sinf(ph*BD_FL*3.0+BD_PHL);
  float rE=center+extR, lE=center-extL, len=rE-lE;
  float comp=1.0-(len-2*BD_MIN)/(2*(BD_MAX-BD_MIN)); comp=comp<0?0:(comp>1?1:comp);
  float pump=0.62+0.45*comp, inten;
  if(x>=lE&&x<=rE)inten=1.0;
  else{float dd=(x<lE)?(lE-x):(x-rE);float e=1.0-dd/BD_EDGE;inten=e>0?e*e:0;}
  return inten*pump;
}

float bloomOne(float age,float d){
  if(age<0||age>=BL_LIFE)return 0;
  float sigma,peak,env;
  if(age<BL_EXPAND){float p=age/BL_EXPAND;float e=1.0-powf(1.0-p,2.0);
    sigma=BL_SIG0+(BL_SIGMAX-BL_SIG0)*e; peak=BL_APEAK+(BL_AFILL-BL_APEAK)*e; env=1.0;}
  else{float fp=(age-BL_EXPAND)/BL_FADE; sigma=BL_SIGMAX; peak=BL_AFILL; env=powf(1.0-fp,1.2);}
  return peak*expf(-(d*d)/(2.0*sigma*sigma))*env;
}
float bloomI(float x,float tg,float center){
  float d=fabs(x-center), k=floorf(tg/BL_PERIOD), s=0; int g=(int)ceilf(BL_LIFE/BL_PERIOD)+1;
  for(int i=0;i<=g;i++)s+=bloomOne(tg-(k-i)*BL_PERIOD,d); return s;
}

float lasOne(float age,float x,float tg){
  if(age<0||age>=LS_LIFE)return 0;
  float h,hb,trail,env,boom=0;
  if(age<LS_CHARGE){float cp=age/LS_CHARGE;float ss=cp*cp*(3-2*cp);
    float vib=1.0+LS_VIB*sinf(tg*LS_VF1)+0.6*LS_VIB*sinf(tg*LS_VF2);
    h=0;hb=LS_HBCH*ss*vib;trail=0;env=1;}
  else if(age<LS_CHARGE+LS_TRAVEL){float p=(age-LS_CHARGE)/LS_TRAVEL;float e=1.0-powf(1.0-p,2.2);
    h=e*(NUM_PIXELS-1);hb=LS_HBLN+(LS_HBMIN-LS_HBLN)*p;trail=LS_FILL*p;boom=LS_BOOM*expf(-p*LS_BOOMD);env=1;}
  else{float fp=(age-LS_CHARGE-LS_TRAVEL)/LS_FADE;h=NUM_PIXELS-1;hb=LS_HBMIN;trail=LS_FILL;float kk=1.0-fp;env=kk*kk;}
  float gate=(h+LS_GATE-x)/LS_GATE;gate=gate<0?0:(gate>1?1:gate);
  float base=trail*gate, dd=x-h;
  float bump=(hb+boom)*expf(-(dd*dd)/(2.0*LS_HEADW*LS_HEADW));
  float rip=0.6*sinf(x*0.9-tg*22.0)+0.4*sinf(x*1.7-tg*15.0+1.3); rip=rip>0?powf(rip,LS_FLSH):0;
  float flash=(trail>0.02)?LS_FLASH*rip*gate:0;
  return (base+bump+flash)*env;
}
float lasI(float x,float tg){
  float k=floorf(tg/LS_PERIOD), s=0; int g=(int)ceilf(LS_LIFE/LS_PERIOD)+1;
  for(int i=0;i<=g;i++)s+=lasOne(tg-(k-i)*LS_PERIOD,x,tg); return s;
}

// Was setup() in the standalone sketch.
void initNeopixel(){ strip.begin(); strip.setBrightness(255); strip.show(); neoLastMicros=micros(); }

// Reset the frame timer so the next updateNeopixel() starts cleanly (e.g. after
// the strip has been blanked while the machine was off).
void neopixelResetTiming(){ neoLastMicros=micros(); }

// Turn all LEDs off. Needed because the strip is powered from 5V standby (always
// powered), so when the machine is off we must actively blank it.
void neopixelClear(){ strip.clear(); strip.show(); }

// Was loop() in the standalone sketch. Call repeatedly; renders at most one
// frame per NEO_FRAME_US. Returns immediately between frames.
void updateNeopixel(){
  unsigned long now=micros();
  unsigned long elapsed=now-neoLastMicros;
  if(elapsed<NEO_FRAME_US) return;          // frame-rate cap
  float dt=elapsed/1000000.0;
  if(dt>0.1) dt=0.1;                         // clamp big gaps (e.g. after idle)
  neoT+=dt; neoLastMicros=now;
  float t=neoT;
  float center=(NUM_PIXELS-1)*0.5;

  // rainbow as subtle blue-family shade drift
  float shadeG=0.16+0.08*sinf(t*SHADE_SPEED);
  float shadeR=0.05+0.05*sinf(t*SHADE_SPEED*0.6+2.0);

  for(int i=0;i<NUM_PIXELS;i++){
    float x=(float)i;

    // deep-blue field: bloom + band, light-accumulated
    float bf=screenf(W_BLOOM*bloomI(x,t,center), W_BAND*bandI(x,t)); if(bf>1)bf=1;
    float blueR=shadeR*bf, blueG=shadeG*bf, blueB=bf;

    // laser accent in its own blue->white heat map
    float lc=lasI(x,t); if(lc>1)lc=1;
    float lr=lc*sstep(0.72,1.0,lc), lg=lc*(0.16+0.82*sstep(0.42,1.0,lc)), lb=lc;

    // composite (screen) -> contrast -> master
    float R=crv(screenf(blueR,W_LASER*lr));
    float G=crv(screenf(blueG,W_LASER*lg));
    float B=crv(screenf(blueB,W_LASER*lb));

    uint8_t Ro=strip.gamma8((uint8_t)constrain(R*MASTER*255.0,0,255));
    uint8_t Go=strip.gamma8((uint8_t)constrain(G*MASTER*255.0,0,255));
    uint8_t Bo=strip.gamma8((uint8_t)constrain(B*MASTER*255.0,0,255));
    strip.setPixelColor(i,Ro,Go,Bo);
  }
  strip.show();
}

#endif // NEOPIXEL_H
