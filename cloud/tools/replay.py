#!/usr/bin/env python3
"""Offline workbench for TiltBox voice sessions.

Pulls logged sessions from the Worker, re-generates games from their transcripts
with other models or an edited system prompt, runs every result through the real
MicroPython interpreter on this Mac (tests/host_run), and optionally stores the
outcome next to the session in the cloud so it shows in the dashboard.

  export TILTBOX_URL=https://tiltbox-cloud.<you>.workers.dev  TILTBOX_ADMIN=<ADMIN_TOKEN>
  replay.py list                               # recent sessions, transcripts, device results
  replay.py export out/                        # wav + json per session for offline listening
  replay.py prompt > my_prompt.txt             # current system prompt to edit
  replay.py run --model openai/gpt-5-mini      # regenerate all (or --ids a,b) with another model
  replay.py run --prompt my_prompt.txt --save  # try an edited prompt, store results in the cloud
  replay.py check code.py                      # run one script in the host interpreter
  replay.py transcribe recording.wav           # test the STT model on a local wav

Nothing here talks to OpenRouter directly: generation goes through the Worker's
/api/generate so the key stays in one place."""
import argparse,json,os,subprocess,sys,time,urllib.request,urllib.error
from pathlib import Path
HERE=Path(__file__).resolve().parent;FW=HERE.parents[1]/'firmware'/'voice-a'
def cfg():
    url=os.environ.get('TILTBOX_URL','').rstrip('/');tok=os.environ.get('TILTBOX_ADMIN','')
    if not url or not tok:sys.exit('Set TILTBOX_URL and TILTBOX_ADMIN (the Worker URL and its ADMIN_TOKEN).')
    return url,tok
def api(path,data=None,raw=False,ctype='application/json'):
    url,tok=cfg();req=urllib.request.Request(url+path,data=data if raw else (json.dumps(data).encode() if data is not None else None),method='POST' if data is not None else 'GET')
    req.add_header('Authorization','Bearer '+tok);req.add_header('Content-Type',ctype)
    try:
        with urllib.request.urlopen(req,timeout=180) as r:return r.read() if raw else json.loads(r.read())
    except urllib.error.HTTPError as e:
        body=e.read().decode(errors='replace');sys.exit(f'HTTP {e.code} for {path}: {body[:400]}')
def host_run():
    exe=FW/'tests'/'host_run'
    if not exe.exists() or exe.stat().st_mtime<(FW/'main'/'embed_api.c').stat().st_mtime:
        subprocess.run([sys.executable,str(FW/'tests'/'build_host_run.py')],check=True)
    return exe
def check_code(code,budget_ms=4000):
    exe=host_run();tmp=Path('/tmp/tiltbox_replay.py');tmp.write_text(code)
    r=subprocess.run([str(exe),str(tmp),str(budget_ms)],capture_output=True,text=True,timeout=budget_ms/1000+20)
    line=next((l for l in r.stdout.splitlines() if l.startswith('HOST_RESULT')),'HOST_RESULT status=unknown')
    status=line.split('status=')[1].split()[0];return status,line,r.stderr[-3000:]
def cmd_list(a):
    d=api(f'/api/sessions?limit={a.limit}')
    for s in d['sessions']:
        ts=time.strftime('%Y-%m-%d %H:%M',time.localtime(s['ts']/1000))
        res=s.get('result_status') or ('CLOUD-ERROR' if s.get('error') else '-')
        print(f"{s['id']}  {ts}  {s.get('audio_seconds','?')}s  {res:8s} {s.get('gen_model','')} {s.get('gen_ms') or ''}ms ${s.get('cost_usd') or 0:.4f}  {(s.get('transcript') or s.get('error') or '')[:90]}")
def cmd_export(a):
    out=Path(a.dir);out.mkdir(parents=True,exist_ok=True);d=api(f'/api/sessions?limit={a.limit}')
    for s in d['sessions']:
        full=api('/api/sessions/'+s['id']);(out/f"{s['id']}.json").write_text(json.dumps(full,indent=2,ensure_ascii=False))
        if full.get('code'):(out/f"{s['id']}.py").write_text(full['code'])
        wav=api(f"/audio/{s['id']}.wav",raw=False) if False else None
        try:
            url,tok=cfg();req=urllib.request.Request(f"{url}/audio/{s['id']}.wav");req.add_header('Authorization','Bearer '+tok)
            with urllib.request.urlopen(req,timeout=60) as r:(out/f"{s['id']}.wav").write_bytes(r.read())
        except urllib.error.HTTPError:pass
        print('exported',s['id'])
def cmd_prompt(a):
    print(api('/api/prompt')['system_prompt'])
def cmd_run(a):
    sessions=api(f'/api/sessions?limit={a.limit}')['sessions']
    if a.ids:sessions=[s for s in sessions if s['id'] in a.ids.split(',')]
    sessions=[s for s in sessions if s.get('transcript')]
    prompt=Path(a.prompt).read_text() if a.prompt else None;pv=(a.prompt_version or (Path(a.prompt).stem if a.prompt else None))
    rows=[]
    for s in sessions:
        body={'transcript':s['transcript']}
        if a.model:body['model']=a.model
        if prompt:body['system_prompt']=prompt;body['prompt_version']=pv
        if a.temperature is not None:body['temperature']=a.temperature
        if a.reasoning:body['reasoning']={'effort':a.reasoning}
        t=time.time();g=api('/api/generate',body)
        if 'error' in g:
            print(f"{s['id']}  GEN-ERROR {g['error'][:120]}");rows.append((s['id'],'gen-error','',0));continue
        status,line,err=check_code(g['code'],a.budget)
        print(f"{s['id']}  {g.get('model','')}  {g.get('ms')}ms  ${g.get('cost') or 0:.4f}  {len(g['code'])} chars  host={status}   {s['transcript'][:60]}")
        if status not in('ok','ended') and a.verbose:print('   ',err.strip().splitlines()[-1] if err.strip() else line)
        if a.out:
            o=Path(a.out);o.mkdir(parents=True,exist_ok=True);(o/f"{s['id']}.{(a.model or 'default').replace('/','_')}.{pv or 'default'}.py").write_text(g['code'])
        if a.save:
            api('/api/replays',{'session_id':s['id'],'gen_model':g.get('model'),'prompt_version':g.get('prompt_version'),'gen_ms':g.get('ms'),'code':g['code'],'finish_reason':g.get('finish_reason'),'usage':g.get('usage'),'cost_usd':g.get('cost'),'host_status':status,'host_output':err[-3000:],'note':a.note or ''})
        rows.append((s['id'],status,g.get('model',''),g.get('cost') or 0))
    ok=sum(1 for r in rows if r[1] in('ok','ended'));print(f"\n{ok}/{len(rows)} ran without exception on the host interpreter; total ${sum(r[3] for r in rows):.4f}")
def cmd_check(a):
    status,line,err=check_code(Path(a.file).read_text(),a.budget);print(line);
    if status not in('ok','ended'):print(err[-1500:])
    sys.exit(0 if status in('ok','ended') else 1)
def cmd_transcribe(a):
    r=api(f'/api/transcribe?lang={a.lang}',Path(a.file).read_bytes(),raw=True,ctype='audio/wav');print(json.dumps(r if isinstance(r,dict) else json.loads(r),indent=2,ensure_ascii=False))
def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter);sp=p.add_subparsers(dest='cmd',required=True)
    x=sp.add_parser('list');x.add_argument('--limit',type=int,default=50);x.set_defaults(f=cmd_list)
    x=sp.add_parser('export');x.add_argument('dir');x.add_argument('--limit',type=int,default=50);x.set_defaults(f=cmd_export)
    x=sp.add_parser('prompt');x.set_defaults(f=cmd_prompt)
    x=sp.add_parser('run');x.add_argument('--model');x.add_argument('--prompt');x.add_argument('--prompt-version');x.add_argument('--ids');x.add_argument('--limit',type=int,default=20)
    x.add_argument('--temperature',type=float);x.add_argument('--reasoning',choices=['low','medium','high']);x.add_argument('--budget',type=int,default=4000);x.add_argument('--out');x.add_argument('--save',action='store_true');x.add_argument('--note');x.add_argument('-v','--verbose',action='store_true');x.set_defaults(f=cmd_run)
    x=sp.add_parser('check');x.add_argument('file');x.add_argument('--budget',type=int,default=4000);x.set_defaults(f=cmd_check)
    x=sp.add_parser('transcribe');x.add_argument('file');x.add_argument('--lang',default='nl');x.set_defaults(f=cmd_transcribe)
    a=p.parse_args();a.f(a)
if __name__=='__main__':main()
