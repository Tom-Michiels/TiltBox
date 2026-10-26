// TiltBox cloud: one Worker that turns a voice recording into a MicroPython game
// via OpenRouter and logs audio (R2) + transcript + code + device outcome (D1).
import { SYSTEM_PROMPT, PROMPT_VERSION, TRANSCRIBE_PROMPT } from "./prompt.js";
import { dashboardHtml } from "./dashboard.js";

const OPENROUTER = "https://openrouter.ai/api/v1/chat/completions";
const MAX_AUDIO_BYTES = 4 * 1024 * 1024;
const MAX_CODE_CHARS = 60000;

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const p = url.pathname;
    try {
      if (p === "/health") return json({ ok: true, prompt_version: PROMPT_VERSION, mock: env.MOCK === "1" });
      // ---- device endpoints ----
      if (p === "/v1/game" && request.method === "POST") return deviceAuth(request, env) || await handleGame(request, env, ctx);
      if (p === "/v1/result" && request.method === "POST") return deviceAuth(request, env) || await handleResult(request, env);
      // ---- admin endpoints ----
      const denied = adminAuth(request, env, url);
      if (p === "/" || p === "/index.html") {
        if (denied) return loginPage();
        const res = html(dashboardHtml());
        // A valid ?token= in the URL becomes the cookie the page's API calls rely on.
        if (url.searchParams.get("token")) res.headers.append("Set-Cookie", `tb_admin=${encodeURIComponent(url.searchParams.get("token"))}; Path=/; Max-Age=31536000; SameSite=Strict${url.protocol === "https:" ? "; Secure" : ""}`);
        return res;
      }
      if (denied) return denied;
      if (p === "/api/sessions" && request.method === "GET") return await listSessions(env, url);
      let m;
      if ((m = p.match(/^\/api\/sessions\/([\w-]+)$/)) && request.method === "GET") return await getSession(env, m[1]);
      if ((m = p.match(/^\/api\/sessions\/([\w-]+)$/)) && request.method === "DELETE") return await deleteSession(env, m[1]);
      if ((m = p.match(/^\/audio\/([\w-]+)\.wav$/))) return await getAudio(env, m[1]);
      if (p === "/api/prompt") return json({ prompt_version: PROMPT_VERSION, system_prompt: SYSTEM_PROMPT, transcribe_prompt: TRANSCRIBE_PROMPT, stt_model: env.STT_MODEL, gen_model: env.GEN_MODEL });
      if (p === "/api/generate" && request.method === "POST") return await apiGenerate(request, env);
      if (p === "/api/transcribe" && request.method === "POST") return await apiTranscribe(request, env);
      if (p === "/api/replays" && request.method === "POST") return await saveReplay(request, env);
      if (p === "/api/replays" && request.method === "GET") return await listReplays(env, url);
      return json({ error: "not found" }, 404);
    } catch (e) {
      console.error("unhandled", e && e.stack || e);
      return json({ error: String(e && e.message || e) }, 500);
    }
  },
};

// ---------- auth ----------
function bearer(request) {
  const a = request.headers.get("Authorization") || "";
  return a.startsWith("Bearer ") ? a.slice(7).trim() : "";
}
function deviceAuth(request, env) {
  if (!env.DEVICE_TOKEN) return json({ error: "DEVICE_TOKEN secret not configured" }, 500);
  return timingSafeEqual(bearer(request), env.DEVICE_TOKEN) ? null : json({ error: "unauthorized" }, 401);
}
function adminAuth(request, env, url) {
  if (!env.ADMIN_TOKEN) return json({ error: "ADMIN_TOKEN secret not configured" }, 500);
  const cookie = (request.headers.get("Cookie") || "").match(/(?:^|;\s*)tb_admin=([^;]+)/);
  const t = bearer(request) || url.searchParams.get("token") || (cookie && decodeURIComponent(cookie[1])) || "";
  return timingSafeEqual(t, env.ADMIN_TOKEN) ? null : json({ error: "unauthorized" }, 401);
}
function timingSafeEqual(a, b) {
  if (typeof a !== "string" || typeof b !== "string" || a.length !== b.length || !a.length) return false;
  let r = 0;
  for (let i = 0; i < a.length; i++) r |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return r === 0;
}
function loginPage() {
  return html(`<!doctype html><meta charset=utf-8><title>TiltBox cloud</title>
<style>body{font:16px system-ui;margin:3em auto;max-width:28em;padding:0 1em}input{width:100%;padding:.5em;font-size:1em}</style>
<h2>TiltBox cloud</h2><form onsubmit="document.cookie='tb_admin='+encodeURIComponent(t.value)+';path=/;max-age=31536000;samesite=strict'+(location.protocol==='https:'?';secure':'');location.href='/';return false">
<p>Admin token<br><input id=t type=password autofocus></p><button>Open dashboard</button></form>`, 401);
}

// ---------- device: voice -> game ----------
async function handleGame(request, env, ctx) {
  const t0 = Date.now();
  const id = newId();
  const device = request.headers.get("X-TiltBox-Device") || "unknown";
  const firmware = request.headers.get("X-TiltBox-Firmware") || "";
  const battery = parseInt(request.headers.get("X-TiltBox-Battery") || "0", 10) || null;
  const lang = new URL(request.url).searchParams.get("lang") || "nl";
  const audio = new Uint8Array(await request.arrayBuffer());
  if (audio.length < 1000 || audio.length > MAX_AUDIO_BYTES) return json({ error: `audio size ${audio.length} out of range` }, 400);
  const wav = wavInfo(audio);
  const audioKey = `audio/${id}.wav`;
  const row = {
    id, ts: t0, device, firmware, battery_mv: battery, audio_key: audioKey, audio_bytes: audio.length,
    audio_seconds: wav.seconds, lang, stt_model: env.STT_MODEL, stt_ms: null, transcript: null,
    gen_model: env.GEN_MODEL, gen_ms: null, prompt_version: PROMPT_VERSION, code: null, code_chars: null,
    finish_reason: null, usage_json: null, cost_usd: null, error: null,
  };
  // Store the audio first so a failing LLM call is still inspectable.
  await env.AUDIO.put(audioKey, audio, { httpMetadata: { contentType: "audio/wav" } });
  let status = 200;
  try {
    const stt = await transcribe(env, audio, lang);
    row.stt_ms = stt.ms; row.transcript = stt.text;
    if (!stt.text || /^\[geen spraak\]/i.test(stt.text) || stt.text.length < 3) throw new Error("no speech recognised");
    const gen = await generate(env, stt.text, {});
    row.gen_ms = gen.ms; row.code = gen.code; row.code_chars = gen.code.length; row.finish_reason = gen.finish_reason;
    row.usage_json = JSON.stringify({ stt: stt.usage, gen: gen.usage }); row.cost_usd = (stt.cost || 0) + (gen.cost || 0);
  } catch (e) {
    row.error = String(e && e.message || e); status = 502;
    console.error("game pipeline", id, row.error);
  }
  ctx.waitUntil(insertSession(env, row));
  const body = row.error ? { id, error: row.error, transcript: row.transcript } : { id, transcript: row.transcript, code: row.code, model: row.gen_model, prompt_version: PROMPT_VERSION, total_ms: Date.now() - t0 };
  return json(body, status);
}

async function handleResult(request, env) {
  const r = await request.json();
  if (!r || typeof r.id !== "string" || !/^[\w-]{8,40}$/.test(r.id)) return json({ error: "bad id" }, 400);
  const status = String(r.status || "").slice(0, 32);
  const output = typeof r.output === "string" ? r.output.slice(0, 4000) : null;
  const ms = Number.isFinite(r.ms) ? Math.floor(r.ms) : null;
  const res = await env.DB.prepare("UPDATE sessions SET result_status=?, result_ms=?, result_output=?, result_ts=? WHERE id=?")
    .bind(status, ms, output, Date.now(), r.id).run();
  return json({ ok: true, updated: res.meta.changes });
}

// ---------- OpenRouter ----------
async function openrouter(env, body) {
  if (!env.OPENROUTER_API_KEY) throw new Error("OPENROUTER_API_KEY secret not configured");
  const t0 = Date.now();
  const res = await fetch(OPENROUTER, {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${env.OPENROUTER_API_KEY}`,
      "Content-Type": "application/json",
      "HTTP-Referer": "https://github.com/Tom-Michiels/TiltBox",
      "X-Title": "TiltBox voice games",
    },
    body: JSON.stringify({ ...body, usage: { include: true } }),
  });
  const text = await res.text();
  let j; try { j = JSON.parse(text); } catch { throw new Error(`OpenRouter HTTP ${res.status}: ${text.slice(0, 200)}`); }
  if (!res.ok || j.error) throw new Error(`OpenRouter ${res.status}: ${(j.error && j.error.message) || text.slice(0, 200)}`);
  const choice = j.choices && j.choices[0];
  if (!choice || !choice.message) throw new Error("OpenRouter: no choices in response");
  return { content: choice.message.content || "", finish_reason: choice.finish_reason, usage: j.usage || null, cost: j.usage && j.usage.cost || 0, ms: Date.now() - t0, model: j.model };
}

async function transcribe(env, audio, lang) {
  if (env.MOCK === "1") return { text: "Maak een spel waarbij een balletje rolt door een doolhof als je de doos kantelt.", ms: 1, usage: null, cost: 0 };
  const r = await openrouter(env, {
    model: env.STT_MODEL,
    temperature: 0,
    max_tokens: 1000,
    messages: [{
      role: "user",
      content: [
        { type: "text", text: TRANSCRIBE_PROMPT + (lang ? ` Expected language: ${lang}.` : "") },
        { type: "input_audio", input_audio: { data: base64(audio), format: "wav" } },
      ],
    }],
  });
  return { text: r.content.trim(), ms: r.ms, usage: r.usage, cost: r.cost };
}

async function generate(env, transcript, opts) {
  const model = opts.model || env.GEN_MODEL;
  const system = opts.system_prompt || SYSTEM_PROMPT;
  if (env.MOCK === "1" && !opts.model) return { code: MOCK_CODE, finish_reason: "stop", ms: 1, usage: null, cost: 0, model };
  const r = await openrouter(env, {
    model,
    temperature: opts.temperature ?? parseFloat(env.GEN_TEMPERATURE || "0.6"),
    max_tokens: opts.max_tokens || parseInt(env.GEN_MAX_TOKENS || "12000", 10),
    messages: [{ role: "system", content: system }, { role: "user", content: transcript }],
    ...(opts.reasoning ? { reasoning: opts.reasoning } : {}),
  });
  if (r.finish_reason && r.finish_reason !== "stop" && r.finish_reason !== "end_turn") throw new Error(`generation finished with '${r.finish_reason}' (truncated?)`);
  const code = cleanCode(r.content);
  if (code.length < 20) throw new Error("generated code is empty");
  if (code.length > MAX_CODE_CHARS) throw new Error(`generated code too long (${code.length} chars)`);
  return { code, finish_reason: r.finish_reason, ms: r.ms, usage: r.usage, cost: r.cost, model: r.model || model };
}

// Strip markdown fences, leading prose, and imports of the shimmed modules.
export function cleanCode(s) {
  s = s.replace(/\r\n/g, "\n");
  const fence = s.match(/```[a-zA-Z]*\n([\s\S]*?)```/);
  if (fence) s = fence[1];
  s = s.replace(/^```[a-zA-Z]*\n?/, "").replace(/\n```\s*$/, "");
  const lines = s.split("\n");
  const out = [];
  for (const line of lines) {
    const m = line.match(/^\s*(?:import\s+([\w.]+)(?:\s+as\s+\w+)?|from\s+([\w.]+)\s+import\s+.+)\s*$/);
    if (m) {
      const mod = (m[1] || m[2]).replace(/^u/, "");
      if (["time", "random", "math"].includes(mod)) { out.push(line.replace(/\S.*$/, "pass  # " + line.trim())); continue; }
    }
    out.push(line);
  }
  return out.join("\n").trim() + "\n";
}

// ---------- admin API ----------
async function listSessions(env, url) {
  const limit = Math.min(parseInt(url.searchParams.get("limit") || "50", 10) || 50, 500);
  const before = parseInt(url.searchParams.get("before") || "0", 10);
  const q = before
    ? env.DB.prepare("SELECT id,ts,device,firmware,battery_mv,audio_bytes,audio_seconds,lang,stt_model,stt_ms,transcript,gen_model,gen_ms,prompt_version,code_chars,finish_reason,cost_usd,error,result_status,result_ms,result_output,result_ts FROM sessions WHERE ts<? ORDER BY ts DESC LIMIT ?").bind(before, limit)
    : env.DB.prepare("SELECT id,ts,device,firmware,battery_mv,audio_bytes,audio_seconds,lang,stt_model,stt_ms,transcript,gen_model,gen_ms,prompt_version,code_chars,finish_reason,cost_usd,error,result_status,result_ms,result_output,result_ts FROM sessions ORDER BY ts DESC LIMIT ?").bind(limit);
  const { results } = await q.all();
  return json({ sessions: results, prompt_version: PROMPT_VERSION });
}
async function getSession(env, id) {
  const s = await env.DB.prepare("SELECT * FROM sessions WHERE id=?").bind(id).first();
  if (!s) return json({ error: "not found" }, 404);
  const { results } = await env.DB.prepare("SELECT * FROM replays WHERE session_id=? ORDER BY ts DESC").bind(id).all();
  return json({ ...s, replays: results });
}
async function deleteSession(env, id) {
  const s = await env.DB.prepare("SELECT audio_key FROM sessions WHERE id=?").bind(id).first();
  if (!s) return json({ error: "not found" }, 404);
  if (s.audio_key) await env.AUDIO.delete(s.audio_key);
  await env.DB.batch([env.DB.prepare("DELETE FROM replays WHERE session_id=?").bind(id), env.DB.prepare("DELETE FROM sessions WHERE id=?").bind(id)]);
  return json({ ok: true });
}
async function getAudio(env, id) {
  const obj = await env.AUDIO.get(`audio/${id}.wav`);
  if (!obj) return json({ error: "not found" }, 404);
  return new Response(obj.body, { headers: { "Content-Type": "audio/wav", "Cache-Control": "private, max-age=3600", "Content-Length": String(obj.size) } });
}
async function apiGenerate(request, env) {
  const b = await request.json();
  if (!b || typeof b.transcript !== "string") return json({ error: "transcript required" }, 400);
  try {
    const g = await generate(env, b.transcript, { model: b.model, system_prompt: b.system_prompt, temperature: b.temperature, max_tokens: b.max_tokens, reasoning: b.reasoning });
    return json({ ...g, prompt_version: b.system_prompt ? (b.prompt_version || "custom") : PROMPT_VERSION });
  } catch (e) { return json({ error: String(e.message || e) }, 502); }
}
async function apiTranscribe(request, env) {
  const audio = new Uint8Array(await request.arrayBuffer());
  const lang = new URL(request.url).searchParams.get("lang") || "nl";
  try { return json(await transcribe(env, audio, lang)); } catch (e) { return json({ error: String(e.message || e) }, 502); }
}
async function saveReplay(request, env) {
  const r = await request.json();
  const id = newId();
  await env.DB.prepare("INSERT INTO replays (id,session_id,ts,gen_model,prompt_version,gen_ms,code,finish_reason,usage_json,cost_usd,host_status,host_output,note) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)")
    .bind(id, r.session_id || null, Date.now(), r.gen_model || null, r.prompt_version || null, r.gen_ms ?? null, r.code || null, r.finish_reason || null,
      r.usage ? JSON.stringify(r.usage) : null, r.cost_usd ?? null, r.host_status || null, (r.host_output || "").slice(0, 4000), (r.note || "").slice(0, 500)).run();
  return json({ ok: true, id });
}
async function listReplays(env, url) {
  const sid = url.searchParams.get("session");
  const q = sid ? env.DB.prepare("SELECT * FROM replays WHERE session_id=? ORDER BY ts DESC LIMIT 200").bind(sid) : env.DB.prepare("SELECT * FROM replays ORDER BY ts DESC LIMIT 200");
  return json({ replays: (await q.all()).results });
}

// ---------- storage helpers ----------
async function insertSession(env, r) {
  try {
    await env.DB.prepare(`INSERT INTO sessions (id,ts,device,firmware,battery_mv,audio_key,audio_bytes,audio_seconds,lang,stt_model,stt_ms,transcript,gen_model,gen_ms,prompt_version,code,code_chars,finish_reason,usage_json,cost_usd,error)
      VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`)
      .bind(r.id, r.ts, r.device, r.firmware, r.battery_mv, r.audio_key, r.audio_bytes, r.audio_seconds, r.lang, r.stt_model, r.stt_ms, r.transcript, r.gen_model, r.gen_ms, r.prompt_version, r.code, r.code_chars, r.finish_reason, r.usage_json, r.cost_usd, r.error).run();
  } catch (e) { console.error("D1 insert failed", r.id, e && e.message); }
}
function wavInfo(a) {
  // Minimal RIFF parse: find "fmt " and "data" chunks.
  const dv = new DataView(a.buffer, a.byteOffset, a.byteLength);
  let rate = 16000, bits = 16, ch = 1, dataLen = Math.max(0, a.length - 44);
  if (a.length > 12 && String.fromCharCode(a[0], a[1], a[2], a[3]) === "RIFF") {
    let off = 12;
    while (off + 8 <= a.length) {
      const tag = String.fromCharCode(a[off], a[off + 1], a[off + 2], a[off + 3]);
      const len = dv.getUint32(off + 4, true);
      if (tag === "fmt " && off + 24 <= a.length) { ch = dv.getUint16(off + 10, true); rate = dv.getUint32(off + 12, true); bits = dv.getUint16(off + 22, true); }
      if (tag === "data") { dataLen = Math.min(len, a.length - off - 8); break; }
      off += 8 + len + (len & 1);
    }
  }
  const bytesPerSec = rate * ch * (bits / 8) || 32000;
  return { rate, bits, ch, seconds: Math.round(dataLen / bytesPerSec * 100) / 100 };
}
function base64(bytes) {
  let s = "";
  const CH = 0x8000;
  for (let i = 0; i < bytes.length; i += CH) s += String.fromCharCode.apply(null, bytes.subarray(i, i + CH));
  return btoa(s);
}
function newId() {
  const t = Date.now().toString(36);
  const r = crypto.getRandomValues(new Uint8Array(6));
  return t + "-" + Array.from(r, b => b.toString(16).padStart(2, "0")).join("");
}
function json(o, status = 200) { return new Response(JSON.stringify(o), { status, headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" } }); }
function html(s, status = 200) { return new Response(s, { status, headers: { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" } }); }

const MOCK_CODE = `# MOCK game: tilt ball with trail (no OpenRouter call was made)
px, py = 3.5, 3.5
vx, vy = 0.0, 0.0
last = millis()
hue = 0
while True:
    now = millis()
    dt = min(now - last, 100)
    last = now
    tx, ty, tz = tilt()
    vx = (vx + tx * 0.00005 * dt) * 0.96
    vy = (vy + ty * 0.00005 * dt) * 0.96
    px += vx * dt * 0.05
    py += vy * dt * 0.05
    if px < 0: px, vx = 0, -vx * 0.5
    if px > 7: px, vx = 7, -vx * 0.5
    if py < 0: py, vy = 0, -vy * 0.5
    if py > 7: py, vy = 7, -vy * 0.5
    matrix_clear()
    r, g, b = hsv(hue & 255, 255, 120)
    matrix_set(int(px + 0.5), int(py + 0.5), r, g, b)
    hue += 2
    matrix_show()
    sleep_ms(25)
`;
