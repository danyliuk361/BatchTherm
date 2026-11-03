#include "sd_stream.h"

enum { READ_TOKEN=1, READ_BYTES, WRITE_BYTES, WRITE_BUSY };
static uint8_t transfer(sd_stream *s, uint8_t byte) {
  return s->card->bus.transfer(s->card->bus.ctx,byte);
}
static bool finish(sd_stream *s, bt_result result) {
  s->card->bus.select(s->card->bus.ctx,false);
  transfer(s,0xff);
  s->phase=0;
  s->result=result;
  return true;
}
bool sd_stream_begin(sd_stream *s, sd_card *card, uint32_t sector, uint8_t data[512], bool write) {
  if (s->phase || !card->ready) return false;
  *s=(sd_stream){.card=card,.data=data,.result=BT_IO};
  card->bus.select(card->bus.ctx,true);
  transfer(s,0xff);
  transfer(s,write?0x58:0x51);
  for (int i=24;i>=0;i-=8) transfer(s,(uint8_t)(sector>>i));
  transfer(s,1);
  uint8_t response=0xff;
  for (unsigned i=0;i<10 && (response&0x80u);++i) response=transfer(s,0xff);
  if (response) { finish(s,BT_IO); return false; }
  if (write) { transfer(s,0xff); transfer(s,0xfe); }
  s->phase=write?WRITE_BYTES:READ_TOKEN;
  s->since=card->bus.now_ms(card->bus.ctx);
  return true;
}
bool sd_stream_step(sd_stream *s) {
  if (!s->phase) return true;
  uint32_t now=s->card->bus.now_ms(s->card->bus.ctx);
  if (s->phase==READ_TOKEN) {
    uint8_t token=transfer(s,0xff);
    if (token==0xfe) s->phase=READ_BYTES;
    else if (token!=0xff) return finish(s,BT_IO);
    else if (now-s->since>200u) return finish(s,BT_TIMEOUT);
    return false;
  }
  if (s->phase==WRITE_BUSY) {
    if (transfer(s,0xff)==0xff) return finish(s,BT_OK);
    if (now-s->since>500u) return finish(s,BT_TIMEOUT);
    return false;
  }
  bool writing=s->phase==WRITE_BYTES;
  for (unsigned i=0;i<16 && s->position<512u;++i,++s->position) {
    uint8_t rx=transfer(s,writing?s->data[s->position]:0xff);
    if (!writing) s->data[s->position]=rx;
  }
  if (s->position!=512u) return false;
  transfer(s,0xff); transfer(s,0xff);
  if (!writing) return finish(s,BT_OK);
  if ((transfer(s,0xff)&31u)!=5u) return finish(s,BT_IO);
  s->since=now;
  s->phase=WRITE_BUSY;
  return false;
}
