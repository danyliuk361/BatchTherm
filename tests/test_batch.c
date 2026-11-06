#include "batch.h"
#include "sd_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const thermal_profile demo={600,20,350,5};

static void thermal_cycle(void) {
  batch b={0};
  assert(profile_valid(demo));
  assert(!profile_valid((thermal_profile){400,50,350,5}));
  assert(!profile_valid((thermal_profile){751,20,350,30}));
  assert(!profile_valid((thermal_profile){600,20,350,121}));
  assert(!profile_valid((thermal_profile){600,20,199,5}));
  batch_begin(&b,1,demo,0,250);
  assert(!batch_frame(&b,100,579));
  assert(batch_frame(&b,200,580) && b.state==SOAK);
  assert(!batch_frame(&b,450,620) && b.held==250);
  assert(batch_frame(&b,701,600) && b.why==FRAME_GAP && b.held==0);
  assert(batch_frame(&b,800,600) && b.state==SOAK);
  assert(batch_frame(&b,900,621) && b.state==RAMP && b.why==OUT_OF_BAND && b.held==0);
  assert(batch_frame(&b,1000,600));
  for (uint32_t now=1100;now<6000;now+=100) assert(!batch_frame(&b,now,600));
  assert(batch_frame(&b,6000,600) && b.state==COOL && b.held==5000);
  assert(!batch_frame(&b,6100,351));
  assert(batch_frame(&b,6200,350) && b.state==DONE);
  assert(b.minimum==250 && b.maximum==621);
  assert(!batch_interrupt(&b,LID_OPEN,6300));
  batch_begin(&b,2,demo,UINT32_MAX-300,250);
  assert(batch_frame(&b,UINT32_MAX-100,600));
  assert(!batch_frame(&b,49,600) && b.held==150);
  assert(batch_interrupt(&b,LID_OPEN,60) && !batch_active(&b));
  assert(!batch_frame(&b,100,350) && b.state==INTERRUPTED);
  batch_begin(&b,3,demo,10,250);
  assert(!batch_deadline(&b,180009));
  assert(batch_deadline(&b,180010) && b.why==RAMP_TIMEOUT);
  b.state=COOL; b.entered=20;
  assert(batch_deadline(&b,180020) && b.why==COOL_TIMEOUT);
  puts("PASS FR-02..05: profile edges, inclusive band, 250/251 ms gap, reset hold, wrap, cool threshold, phase timeouts, no resume");
}

static void torn_sectors(void) {
  uint8_t sector[512], partial[512], prior[512];
  journal_record src={.sequence=1,.id=1,.time=777,.held=5000,.profile=demo,
    .sample=347,.ambient=241,.minimum=245,.maximum=622,.state=DONE,.why=NORMAL},r;
  journal_encode(prior,&src);
  ++src.sequence; ++src.id; src.state=RAMP; src.held=0;
  journal_encode(sector,&src);
  assert(journal_decode(&r,sector,2) && r.id==2 && r.sample==347 && r.maximum==622 && r.profile.hold_s==5);
  assert(!journal_decode(&r,sector,3));
  for (unsigned cut=0;cut<512;++cut) {
    memset(partial,0,512);
    memcpy(partial,sector,cut);
    assert(!journal_decode(&r,partial,2));
    assert(journal_decode(&r,prior,1) && r.state==DONE);
  }
  for (unsigned bit=0;bit<64u*8u;++bit) {
    sector[bit/8]^=(uint8_t)(1u<<(bit%8));
    assert(!journal_decode(&r,sector,2));
    sector[bit/8]^=(uint8_t)(1u<<(bit%8));
  }
  puts("PASS FR-06/08: logical 64-byte format, sequence and CRC, 512 cut positions, 512 single-bit faults, earlier confirmed record intact");
}

typedef struct {
  uint8_t bytes[512], command[6];
  unsigned position, stage, commands, clocks;
  uint32_t now;
  bool select, no_token, busy;
} fake_sd;
static void select_fake(void *ctx,bool selected) {
  fake_sd *f=ctx;f->select=selected;f->stage=0;f->position=0;
}
static uint32_t now_fake(void *ctx) { return ((fake_sd *)ctx)->now; }
static uint8_t transfer_fake(void *ctx,uint8_t v) {
  fake_sd *f=ctx;++f->clocks;
  if(!f->select)return 0xff;
  if(!f->stage) {
    if(f->position || v==0x51 || v==0x58) {
      f->command[f->position++]=v;
      if(f->position==6) {f->stage=1;f->position=0;++f->commands;}
    }
    return 0xff;
  }
  if(f->stage==1){f->stage=2;return 0;}
  if(f->stage==2) {
    if(f->command[0]==0x51) {if(f->no_token)return 0xff;f->stage=3;return 0xfe;}
    if(v==0xfe)f->stage=3;
    return 0xff;
  }
  if(f->stage==3) {
    uint8_t out=f->bytes[f->position];
    if(f->command[0]==0x58)f->bytes[f->position]=v;
    if(++f->position==512){f->stage=4;f->position=0;}
    return out;
  }
  if(f->stage==4){if(++f->position==2)f->stage=5;return 0xff;}
  if(f->stage==5){f->stage=6;return 5;}
  return f->busy?0:0xff;
}
static void transfer_steps(void) {
  fake_sd f={0};
  sd_card c={.bus={&f,transfer_fake,select_fake,now_fake},.ready=true};
  sd_stream s={0};
  uint8_t b[512];
  for(unsigned i=0;i<512;++i)b[i]=(uint8_t)(i^0x95u);
  assert(sd_stream_begin(&s,&c,JOURNAL_FIRST,b,true));
  for(unsigned n=0;n<32;++n) {
    unsigned start=f.clocks;
    assert(!sd_stream_step(&s));assert(f.clocks-start<=19u);++f.now;
  }
  assert(sd_stream_step(&s)&&s.result==BT_OK&&!f.select);
  memset(b,0,512);
  assert(sd_stream_begin(&s,&c,JOURNAL_FIRST,b,false));
  for(unsigned n=0;n<32;++n)assert(!sd_stream_step(&s));
  assert(sd_stream_step(&s)&&s.result==BT_OK&&!memcmp(f.bytes,b,512));
  f.no_token=true;
  assert(sd_stream_begin(&s,&c,JOURNAL_FIRST,b,false));
  f.now+=201;
  assert(sd_stream_step(&s)&&s.result==BT_TIMEOUT&&!f.select);
  f.busy=true;
  assert(sd_stream_begin(&s,&c,JOURNAL_FIRST,b,true));
  for(unsigned n=0;n<32;++n)assert(!sd_stream_step(&s));
  f.now+=501;
  assert(sd_stream_step(&s)&&s.result==BT_TIMEOUT&&!f.select);
  puts("PASS cooperative CMD17/CMD24: payload round trip, bounded burst, token/busy timeouts release CS");
}
int main(void) { thermal_cycle();torn_sectors();transfer_steps(); }
