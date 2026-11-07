// MCU clock drives the scenario. Browser receipt time is used only by the
// automation watchdog, never as a firmware latency measurement.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {EventEmitter}=require('node:events');
const home=require('node:os').homedir();
const pw=process.env.PLAYWRIGHT_MODULE||path.join(home,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const defaultChrome=path.join(home,'Library/Caches/ms-playwright/chromium-1248/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing');
const {chromium}=require(pw);
const mode=process.argv[2],out=path.resolve(process.argv[3]||'runs/lab3');
const root=path.resolve(__dirname,'..');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const emitter=new EventEmitter();
const lines=[];let fragment='',heart=null,uartBytes=0;
const field=(line,key)=>Number(line?.match(new RegExp('(?:^| )'+key+'=(-?\\d+)'))?.[1]);
let failed=null;const images=[];
function until(predicate,ms=90000) {
  const found=predicate();
  if(found) return Promise.resolve(found);
  return new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>{emitter.off('data',check);reject(Error('MCU stimulus deadline expired'));},ms);
    function check(){try{const result=predicate();if(!result)return;clearTimeout(timer);emitter.off('data',check);resolve(result);}
    catch(error){clearTimeout(timer);emitter.off('data',check);reject(error);}}
    emitter.on('data',check);
  });
}
const after=(start,re,ms)=>until(()=>lines.slice(start).find(line=>re.test(line)),ms);
const mcu=()=>field(heart,'t')||0;
const waitMCU=(t,ms)=>until(()=>mcu()>=t,ms);
const clock=()=>new Date().toISOString();
let output,stimuli;
function note(action,data={}){fs.appendFileSync(stimuli,JSON.stringify({utc:clock(),mcu_last_heartbeat_ms:mcu(),action,...data})+'\n');}
(async()=>{
  fs.mkdirSync(path.dirname(out),{recursive:true});
  fs.mkdirSync(out); // never overwrite a failed run
  stimuli=path.join(out,'stimuli.jsonl');
  output=fs.openSync(path.join(out,'uart.log'),'wx');
  const firmwareDir=path.resolve(process.env.FIRMWARE_DIR||path.join(root,'firmware/build'));
  const hex=fs.readFileSync(path.join(firmwareDir,'firmware.hex'));
  const elf=fs.readFileSync(path.join(firmwareDir,'firmware.elf'));
  const diagram=JSON.parse(fs.readFileSync(path.join(root,'diagram.json'),'utf8'));
  diagram.parts.find(p=>p.id==='sample').attrs.temperature='25';
  if(['s1','s2','rehearsal','full','timeouts'].includes(mode)) {
    // Preserve ADC, lid, active state and SD occupancy for the whole run.
    diagram.connections=diagram.connections.filter(c=>!['logic:D0','logic:D1','logic:D2','logic:D7'].includes(c[0]));
  }
  if(mode==='s2'){
    const analyzer=diagram.parts.find(p=>p.id==='logic');
    const names=analyzer.attrs.channelNames.split(',');names[0]='SOAK_LED';
    analyzer.attrs.channelNames=names.join(',');
    diagram.connections.push(['logic:D0','nucleo:PA5','blue',[]]);
  }
  if(mode==='fault'||mode==='startup') {
    diagram.parts.push({type:'wokwi-pushbutton',id:'busfault',top:325,left:-275,attrs:{label:'SDA LOW',bounce:'0',key:'F'}});
    diagram.connections.push(['busfault:1.l','nucleo:PB9','orange',[]],['busfault:2.l','nucleo:GND.1','black',[]]);
  }
  if(mode==='startup') {
    diagram.parts=diagram.parts.filter(p=>p.id!=='busfault');
    diagram.connections=diagram.connections.filter(c=>!c[0].startsWith('busfault:'));
    diagram.parts.push({type:'wokwi-slide-switch',id:'startup-fault',top:335,left:-275,attrs:{value:'',bounce:'0'}});
    diagram.connections.push(['startup-fault:2','nucleo:PB9','orange',[]],['startup-fault:1','nucleo:GND.1','black',[]]);
  }
  if(mode==='nosd'){diagram.parts=diagram.parts.filter(p=>p.id!=='storage');diagram.connections=diagram.connections.filter(c=>!c[0].startsWith('storage:'));}
  const diagramText=JSON.stringify(diagram);
  fs.writeFileSync(path.join(out,'diagram.json'),diagramText);
  const sdImage=fs.readFileSync(process.env.SD_IMAGE||path.join(root,'docs/inputs/sd-demo.img'));
  fs.writeFileSync(path.join(out,'sd-before.img'),sdImage);
  const run={mode,started:clock(),pid:process.pid,version:'4.1.0',elf_sha256:hash(elf),hex_sha256:hash(hex),
    diagram_sha256:hash(diagramText),sd_before_sha256:hash(sdImage),browser_transport:'separate Chromium process, CDP pipe',status:'running'};
  const save=()=>fs.writeFileSync(path.join(out,'run.json'),JSON.stringify(run,null,2));
  save();
  let browser,page,stopped=false;
  const events=[];
  const eventPath=path.join(out,'events.jsonl');
  const event=m=>{events.push(m.command);fs.appendFileSync(eventPath,JSON.stringify({utc:clock(),...m})+'\n');};
  try {
    browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_EXECUTABLE||defaultChrome});
    page=await browser.newPage({viewport:{width:1550,height:1260},deviceScaleFactor:1});
    await page.exposeFunction('resultEvent',m=>{
      if(m.command==='uartBatch') {
        const bytes=Buffer.from(m.bytes);fs.writeSync(output,bytes);uartBytes+=bytes.length;
        fragment+=bytes.toString('ascii');
        let idx;
        while((idx=fragment.indexOf('\n'))>=0){
          const line=fragment.slice(0,idx).replace(/\r$/,'');fragment=fragment.slice(idx+1);
          lines.push(line);
          if(line.startsWith('HEART '))heart=line;

        }
        emitter.emit('data');
      } else if(m.command==='sdcard:export'){
        if(m.image)fs.writeFileSync(path.join(out,'sd-after.img'),Buffer.from(m.image,'base64'));
        event({command:m.command,received:!!m.image});
      } else if(m.command==='vcd:ready'){
        fs.writeFileSync(path.join(out,'capture.vcd'),Buffer.from(m.vcd));event({command:m.command,bytes:m.vcd.length});
      } else {
        if(m.command==='sim:stop')stopped=true;
        event(m);
      }
    });
    await page.addInitScript(()=>window.addEventListener('message',e=>{
      if(e.data?.command!=='start'||!e.data.port)return;
      const port=window.__wokwiPort=e.data.port;let pending=[];
      const flush=()=>{if(pending.length){window.resultEvent({command:'uartBatch',bytes:pending});pending=[];}};
      setInterval(flush,35);
      port.onmessage=ev=>{
        const d=ev.data;
        if(d.command==='uartData'){for(const b of new Uint8Array(d.bytes))pending.push(b);return;}
        flush();
        if(d.command==='sim:stop')port.postMessage({command:'vcd:request'});
        const o={};
        for(const [k,v] of Object.entries(d))
          o[k]=v instanceof ArrayBuffer?Array.from(new Uint8Array(v)):ArrayBuffer.isView(v)?Array.from(v):v;
        window.resultEvent(o);
      };
    }));
    page.on('pageerror',e=>event({command:'page-error',message:String(e).slice(0,220)}));
    await page.goto('https://wokwi.com/vscode/wcode?v=4.1.0');
    await page.waitForFunction(()=>!!window.__wokwiPort,{timeout:20000});
    const token=fs.readFileSync(path.join(home,'.wokwi/user.tok'),'utf8').split('\n').find(x=>x.startsWith('key='));
    if(!token)throw Error('Existing Wokwi license missing');
    await page.evaluate(m=>window.__wokwiPort.postMessage(m),{command:'start',license:token.slice(4),diagram:diagramText,
       firmware:hex.toString('utf8'),firmwareFormat:'hex',chips:[],useGateway:false,disableSerialMonitor:false,hidePersonalInfo:true,
       sdcards:mode==='nosd'?[]:[{part:'storage',image:sdImage.toString('base64'),sizeBytes:sdImage.length,export:'image'}]});
    const sendRaw=text=>page.evaluate(v=>window.__wokwiPort.postMessage({command:'uartData',bytes:new TextEncoder().encode(v).buffer}),text);
    async function send(text,paced=false){note('uart',{text,paced});if(paced){for(const c of text){await sendRaw(c);await sleep(12);}}else await sendRaw(text);}
    async function exchange(text,re,ms){const start=lines.length;await send(text+'\n');return after(start,re,ms);}
    async function snap(name){await page.screenshot({path:path.join(out,name+'.png')});images.push(name);}
    let closed=false;
    async function lid(wanted) {
      if(closed===wanted)return;
      const el=page.locator('wokwi-pushbutton').first();await el.scrollIntoViewIfNeeded();
      note('lid',{closed:wanted});
      const index=lines.length;
      await el.click({modifiers:wanted?['Meta']:[]});
      await after(index,new RegExp('^LID .*closed='+Number(wanted)+'$'),10000);
      closed=wanted;
    }
    async function temp(c){
      const slider=page.locator('input[type=range]').first();
      if(!(await slider.isVisible())) await page.locator('wokwi-ntc-temperature-sensor').click();
      await slider.evaluate((el,value)=>{Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(el,String(value));
        el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));},c);
      note('ntc',{c});
    }
    const ready=await after(0,mode==='nosd'?/^SD init=[1-9]/:/^JOURNAL ready=/,mode==='full'?600000:150000);
    console.log('boot',await after(0,/^BOOT /,30000),ready);
    if(mode!=='startup') console.log('first',await after(0,/^FIRST /,15000));
    if(mode==='probe'){
      console.log('status',await exchange('status',/^ACK cmd=status /,5000));
      console.log('open gate',await exchange('start',/^ERR cmd=start why=LID/,5000));
      await lid(true);
      await exchange('profile 600 20 4 350',/^ERR cmd=profile why=RANGE/,5000);
      await exchange('profile 600 20 5 350',/^ACK cmd=profile /,5000);
      const start=lines.length;
      await send('start\n'); await after(start,/^ACK cmd=start /,12000);
      await exchange('profile 650 20 5 350',/^ERR cmd=profile why=ACTIVE/,5000);
      await temp(60); await after(start,/^EV .*phase=2 /,12000);
      await snap('soak');
      const departure=lines.length;
      await temp(63);await after(departure,/^EV .*phase=1 why=11 hold=0 /,12000);
      const resumed=lines.length;
      await temp(58);await after(resumed,/^EV .*phase=2 /,12000);
      await temp(62);
      await after(start,/^EV .*phase=3 /,18000);
      await temp(25);await after(start,/^EV .*phase=4 /,12000);
      await after(start,/^COMMIT .*phase=4 /,12000);
      await exchange('export',/^EXPORT end=6$/,22000);
      await exchange('x'.repeat(84),/^ERR cmd=line why=OVERFLOW_OR_LENGTH/,8000);
    } else if(mode==='s1'){
      await lid(true);
      await exchange('measure',/^WINDOW /,8000);
      await waitMCU(mcu()+1000,10000);
      const t=mcu();run.measure_start_ms=t;
      await waitMCU(t+Number(process.env.RUN_MS||60000),300000);
    } else if(mode==='s2'||mode==='rehearsal'){
      await lid(true);
      await exchange('measure',/^WINDOW /,8000);
      await waitMCU(mcu()+2000,10000);
      run.measure_start_ms=mcu();save();
      if(mode==='s2'){
        waitMCU(run.measure_start_ms+180000,360000).then(()=>{
          fs.writeFileSync(path.join(out,'S2.checkpoint.json'),JSON.stringify({
            utc:clock(),start_ms:run.measure_start_ms,last_ms:mcu(),heart,uart_bytes:uartBytes
          },null,2));
        }).catch(e=>event({command:'checkpoint-failure',message:e.message}));
      }
      const target=run.measure_start_ms+Number(mode==='s2'?3600000:process.env.RUN_MS||240000);
      const statusFlow=(async()=>{for(let t=run.measure_start_ms+10000;t<target;t+=10000){
        await waitMCU(t,45000);await exchange('status',/^ACK cmd=status /,8000);
      }})();statusFlow.catch(()=>{});
      for(let cycle=0;mcu()<target+2000;++cycle){
        const begin=run.measure_start_ms+cycle*120000;
        await waitMCU(begin,240000);
        if(begin+70000<=target+2000){
          const i=lines.length;
          await send('start\n');await after(i,/^ACK cmd=start /,15000);
          await waitMCU(begin+8000,30000);await temp(60);
          await after(i,/^EV .*phase=3 /,100000);
          await waitMCU(begin+55000,60000);await temp(25);
          await after(i,/^COMMIT .*phase=4 /,30000);
        }
        const end=Math.min(begin+120000,target+2000);
        await waitMCU(end,240000);
      }
      await statusFlow;
    } else if(mode==='recover'){
      await exchange('status',/^ACK cmd=status /,8000);
      await exchange('export',/^EXPORT end=/,120000);
    } else if(mode==='cut'){
      await lid(true);
      await exchange('start',/^ACK cmd=start /,12000);
      await send('abort\n');await after(0,/^EV .*why=4 /,10000);
      // Stop mid-flight; actual VCD and preserved image establish cut position.
      await sleep(150);
    } else if(mode==='startup'){
      await lid(true);
      await exchange('start',/^ERR cmd=start why=BMP/,8000);
      const i=lines.length;
      await page.locator('wokwi-slide-switch').click();note('startup_sda_release');
      await after(i,/^FIRST /,12000);
      await waitMCU(mcu()+2500,12000);
    } else if(mode==='fault'){
      await lid(true);
      await exchange('start',/^ACK cmd=start /,12000);
      const before=lines.length;
      await send('nack\n');await after(before,/^EV .*why=3 /,10000);
      await exchange('start',/^ERR cmd=start why=BMP/,8000);
      await exchange('recover',/^BMP .*result=0 /,15000);
      await waitMCU(mcu()+1000,8000);
      const f=page.locator('wokwi-pushbutton').nth(1);
      let fstart=lines.length;
      await f.click({modifiers:['Meta']});note('sda_short',{low:true});
      await after(fstart,/^BMP .*result=2 /,10000);
      await exchange('measure',/^WINDOW /,8000);
      await waitMCU(mcu()+7000,30000);
      await exchange('stats',/^COUNTS /,12000);
      note('sda_timeout_measurement_end');
      fstart=lines.length;
      await f.click();note('sda_short',{low:false});
      await after(fstart,/^BMP .*result=0 /,15000);
      const k=lines.length;
      await send('start\n');await after(k,/^ACK cmd=start /,12000);
      await temp(-5);await after(k,/^EV .*why=2 /,10000);
      await exchange('start',/^ERR cmd=start why=NTC/,8000);
      await temp(25);await waitMCU(mcu()+2000,10000);
    } else if(mode==='nosd'){
      await lid(true);await exchange('start',/^ERR cmd=start why=SD/,8000);
    } else if(mode==='timeouts'){
      await lid(true);
      let i=lines.length;
      await send('start\n');await after(i,/^ACK cmd=start /,15000);
      await after(i,/^COMMIT .*phase=5 why=5 /,250000);
      await exchange('profile 600 20 5 350',/^ACK cmd=profile /,5000);
      i=lines.length;await send('start\n');await after(i,/^ACK cmd=start /,15000);
      await temp(60);await after(i,/^EV .*phase=3 /,15000);
      await after(i,/^COMMIT .*phase=5 why=6 /,250000);
    } else if(mode==='full'){
      await lid(true);await exchange('start',/^ERR cmd=start why=FULL/,8000);
    } else if(mode==='s3'||mode==='s3-abort'||mode==='load'){
      await lid(true);
      await waitMCU(mcu()+1000,10000);
      await exchange('measure',/^WINDOW /,8000);
      const initial=mcu();run.measure_start_ms=initial;
      await send('export\n');
      let stream=true,active=null;
      const rate=Number(process.env.RATE||10);
      const sendStart=performance.now();let sent=0;
      const pump=(async()=>{while(stream){
        await sleep(Math.max(0,sendStart+sent*1000/rate-performance.now()));
        if(!stream)break;
        await sendRaw('status\n');note('status_tx',{sequence:++sent});
      }})();
      note('status_stream',{rate_host_hz:rate});
      try{
        if(mode==='s3'){
          for(let n=0;n<Number(process.env.LID_TRIALS||36);++n){
            const i=lines.length;
            await send('start\n');await after(i,/^EV .*phase=1 /,12000);
            await sleep(55+(n*79)%205);
            await lid(false);await after(i,/^EV .*why=1 /,12000);
            await lid(true);
            await after(i,/^COMMIT .*phase=5 why=1 /,15000);
            if(n===9||n===29)await snap('s3-'+n);
          }
          const i=lines.length;
          await send('start\n');await after(i,/^ACK cmd=start /,20000);
          await exchange('abort',/^ACK cmd=abort /,20000);
          await waitMCU(Math.max(initial+60000,mcu()+2000),240000);
          await after(0,/^EXPORT end=/,120000);
        }else if(mode==='s3-abort'){
          for(let n=0;n<30;++n){
            const i=lines.length;
            await send('start\n');await after(i,/^EV .*phase=1 /,12000);
            await sleep(55+(n*79)%205);
            await exchange('abort',/^ACK cmd=abort /,20000);
          }
          await waitMCU(Math.max(initial+60000,mcu()+1000),240000);
          await after(0,/^EXPORT end=/,120000);
        }else {
          const end=initial+Number(process.env.RUN_MS||18000);
          if(process.env.FLOOD==='1'){note('burst',{bytes:3500});await sendRaw('status\n'.repeat(500));}
          await waitMCU(end,120000);
        }
      }finally{stream=false;await pump;}
      if(mode==='load'){
        await waitMCU(mcu()+3000,14000);
        await send('\n'); // end the discard-until-LF state after overrun
        await exchange('status',/^ACK cmd=status /,8000);
      }
    } else throw Error('unknown mode');
    if(mode!=='cut')await exchange('stats',/^COUNTS /,12000);
    await snap('wokwi');
    run.measure_end_ms=mcu();
  }catch(e){failed=e;console.error(e.message);run.failure=e.message;}
  finally {
    if(page&&!page.isClosed()){
      if(!stopped)await page.evaluate(()=>window.__wokwiPort?.postMessage({command:'sim:stop'})).catch(()=>{});
      await sleep(2000);
      if(!failed)try{
        const uart=fs.readFileSync(path.join(out,'uart.log'));
        if(!uart.subarray(0,5).equals(Buffer.from('BOOT '))||uart.length!==uartBytes)
          throw Error('UART capture is incomplete');
        const vcd=fs.openSync(path.join(out,'capture.vcd'),'r');
        try{
          const header=Buffer.alloc(4096);fs.readSync(vcd,header,0,header.length,0);
          if(!header.includes('$enddefinitions'))throw Error('VCD header is incomplete');
        }finally{fs.closeSync(vcd);}
        for(const name of images)
          if(fs.readFileSync(path.join(out,name+'.png')).subarray(0,8).toString('hex')!=='89504e470d0a1a0a')
            throw Error('Unreadable screenshot '+name);
        if(mode!=='nosd'&&fs.statSync(path.join(out,'sd-after.img')).size!==sdImage.length)
          throw Error('SD export length mismatch');
      }catch(error){failed=error;run.failure=error.message;console.error(error.message);}
    }
    if(browser)await browser.close();
    fs.closeSync(output);
    run.status=failed?'failed':'captured';run.ended=clock();run.uart_bytes=uartBytes;run.images=images;run.events=events;save();
    console.log('saved',out,'MCU ms',mcu(),'UART bytes',uartBytes,'status',run.status);
  }
  if(failed)process.exitCode=1;
})().catch(e=>{console.error(e.message);process.exitCode=1;});
