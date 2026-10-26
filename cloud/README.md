# TiltBox cloud

A single Cloudflare Worker that turns a TiltBox voice recording into a MicroPython game via
[OpenRouter](https://openrouter.ai) and logs everything for offline analysis:

- `POST /v1/game` (device): WAV in → `{id, transcript, code}` out. Audio goes to R2, metadata +
  transcript + code + token usage + cost to D1.
- `POST /v1/result` (device): how the generated script fared on the box (`running`, `error` with the
  MicroPython traceback, `aborted`, `crash`).
- `GET /` (you): dashboard with audio player, transcript, code, device outcome, latency and cost.
- `GET /api/...` (you): JSON for the offline tool `tools/replay.py`.

Cloudflare storage/compute and OpenRouter calls can incur charges. Check the current
plans, model availability and prices in your own accounts before deployment. The
included model names are the 9 October 2026 configuration, not a pricing guarantee.
The database ID in `wrangler.toml` is deliberately a placeholder; no deployed account,
tokens, recordings or local database are included in this repository.

## One-time setup

```bash
cd cloud/worker
npm install
npx wrangler login                                  # opens the browser once
npx wrangler d1 create tiltbox                       # paste the printed database_id into wrangler.toml
npx wrangler r2 bucket create tiltbox-audio
npm run db:init                                     # creates the tables remotely
npx wrangler secret put OPENROUTER_API_KEY          # from https://openrouter.ai/keys
npx wrangler secret put DEVICE_TOKEN                # any long random string; the TiltBox sends this
npx wrangler secret put ADMIN_TOKEN                 # any long random string; the dashboard asks for this
npm run deploy                                      # prints https://tiltbox-cloud.<you>.workers.dev
```

Then provision the box (Wi-Fi + Worker URL + DEVICE_TOKEN, no OpenRouter key on the device):

```bash
# From the repository root:
cd firmware/voice-a
python3 provision.py /dev/cu.usbmodem101 --url https://tiltbox-cloud.<you>.workers.dev
```

Open the Worker URL in a browser and paste the ADMIN_TOKEN once; it is kept in a cookie.

### Models and prompt

`wrangler.toml` `[vars]`:

| var | default | notes |
|---|---|---|
| `STT_MODEL` | `google/gemini-2.5-flash-lite` | must list *audio* under input modalities on OpenRouter |
| `GEN_MODEL` | `anthropic/claude-sonnet-4.6` | any chat model; `openai/gpt-oss-120b` is the cheap option |
| `GEN_MAX_TOKENS` | 12000 | games of 100-300 lines need 3-8k output tokens |
| `GEN_TEMPERATURE` | 0.6 | |
| `MOCK` | 0 | `1` = no OpenRouter calls, canned transcript and game (for testing the pipeline) |

The system prompt lives in `src/prompt.js`; bump `PROMPT_VERSION` whenever you edit it, because it
is logged with every session. Redeploy with `npm run deploy`.

## Local development (no deploy needed)

```bash
cd cloud/worker
cp .dev.vars.example .dev.vars        # fill in OPENROUTER_API_KEY, DEVICE_TOKEN, ADMIN_TOKEN
npm run db:init:local
npm run dev                            # http://<your-mac-ip>:8787, local D1/R2 under .wrangler/
```

`npm run dev -- --var MOCK:1` runs without an OpenRouter key. The firmware accepts a plain
`http://<mac-ip>:8787` URL for exactly this case (it warns on the console; use HTTPS for anything
beyond your own LAN):

```bash
python3 firmware/voice-a/provision.py /dev/cu.usbmodem101 --cloud-only --url http://192.168.1.23:8787
```

## Offline work on the prompt and models

```bash
export TILTBOX_URL=https://tiltbox-cloud.<you>.workers.dev TILTBOX_ADMIN=<ADMIN_TOKEN>
cd cloud/tools
./replay.py list                                  # sessions, transcripts, device outcome, cost
./replay.py export ../../output/sessions          # wav + transcript + code per session
./replay.py prompt > ../prompts/try1.txt          # edit it
./replay.py run --prompt ../prompts/try1.txt --save --note "shorter examples"
./replay.py run --model openai/gpt-oss-120b --save
./replay.py run --model google/gemini-2.5-pro --reasoning low --ids <id1>,<id2> -v
./replay.py check some_game.py                    # one script through the host interpreter
```

`run` regenerates games from the logged transcripts through the Worker (so the OpenRouter key stays
there), executes each result for a few seconds in the real MicroPython interpreter on your Mac with
a simulated tilting box (`firmware/voice-a/tests/host_run`), and prints `host=ok|error|no_show|ended`.
With `--save` the replay is stored next to the session and shows under *code* in the dashboard.
The host run proves syntax, API usage and "does it keep drawing", not fun; listen to the audio and
read the code for that.

## Privacy

Recordings, transcripts and generated code are stored in your own Cloudflare account until you
delete them (dashboard *delete* button or `DELETE /api/sessions/<id>`). Audio is also sent to the
OpenRouter provider of the STT model. The device sends a short device id (last MAC bytes), firmware
version and battery voltage with each request.

Keep `.dev.vars`, `.wrangler/`, session exports and flash/NVS dumps private. Do not
commit real API keys or device/admin tokens. Use HTTPS for deployment; the optional
plain-HTTP development mode exposes audio and bearer tokens to the local network.
