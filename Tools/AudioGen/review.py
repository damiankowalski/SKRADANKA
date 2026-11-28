#!/usr/bin/env python3
"""The listening room: a local page that plays a series and saves the owner's verdicts (Tools/AudioGen/README.md).

    .venv/bin/python review.py first            # http://127.0.0.1:8765, opens the browser
    .venv/bin/python review.py first --port 9000 --no-browser

Per clip the page shows the player (the finished clip, as the game plays it, and the model's raw output), the
waveform, the prompt, the seed and the checks, and takes a verdict: accept, reject or change, with a note. Every click
saves out/<series>/feedback.json at once; promote.py carries the verdicts out. The server listens on 127.0.0.1 only
and serves nothing outside the series folder. Fallback without a browser: the files in Finder and feedback.json by hand.
"""

import argparse
import datetime
import json
import sys
import threading
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
DECISIONS = ("accept", "reject", "change")


# ------------------------------------------------------------------------------------------------ feedback file

def feedback_path(series):
    return series / "feedback.json"


def load_feedback(series):
    """{"series", "updated", "clips": {clip stem: {"decision", "note", "date"}}}; empty when nothing was rated."""
    path = feedback_path(series)
    if not path.is_file():
        return {"series": series.name, "updated": None, "clips": {}}
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    data.setdefault("clips", {})
    return data


def set_verdict(series, clip, decision, note):
    """Records one verdict (decision None clears it). Returns the whole feedback; raises ValueError on bad input."""
    clips = {c["file"][:-4] for c in load_series(series)["clips"]}
    if clip not in clips:
        raise ValueError(f"no clip {clip} in the series")
    if decision is not None and decision not in DECISIONS:
        raise ValueError(f"decision must be one of {', '.join(DECISIONS)}")
    note = (note or "").strip()
    if decision == "change" and not note:
        raise ValueError("a change needs a note: what should be different")
    feedback = load_feedback(series)
    now = datetime.datetime.now().isoformat(timespec="seconds")
    if decision is None:
        feedback["clips"].pop(clip, None)
    else:
        feedback["clips"][clip] = {"decision": decision, "note": note, "date": now}
    feedback["updated"] = now
    feedback_path(series).write_text(json.dumps(feedback, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return feedback


def load_series(series):
    path = series / "series.json"
    if not path.is_file():
        raise FileNotFoundError(f"{path} is missing: generate the series first (generate.py)")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


# ------------------------------------------------------------------------------------------------ the server

PAGE = """<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AudioGen: odsłuch</title>
<style>
:root{--bg:#16181b;--card:#202328;--ink:#e6e6e6;--dim:#9aa0a8;--ok:#5fbf7a;--bad:#e06c6c;--chg:#e0b050;--line:#33373e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,sans-serif}
header{padding:16px 20px;border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:2}
h1{font-size:18px;margin:0}header p{margin:4px 0 0;color:var(--dim)}
main{padding:16px 20px;display:grid;gap:12px;max-width:1100px}
h2{font-size:15px;margin:18px 0 0;color:var(--dim);font-weight:600}
.clip{background:var(--card);border:1px solid var(--line);border-left:4px solid var(--line);border-radius:8px;padding:12px 14px}
.clip.accept{border-left-color:var(--ok)}.clip.reject{border-left-color:var(--bad)}.clip.change{border-left-color:var(--chg)}
.top{display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}.top b{font-size:15px}.dim{color:var(--dim)}
.fail{color:var(--bad)}.pass{color:var(--ok)}
canvas{width:100%;height:64px;display:block;margin:8px 0;background:#121417;border-radius:4px;cursor:pointer}
.players{display:flex;gap:14px;flex-wrap:wrap;align-items:center}audio{height:32px}
.prompt{margin:6px 0;font-style:italic}.measures{font:12px ui-monospace,monospace;color:var(--dim)}
.verdict{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap;align-items:center}
button{background:#2b2f36;color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:6px 12px;font-size:14px;cursor:pointer}
button.on.accept{background:var(--ok);color:#111}button.on.reject{background:var(--bad);color:#111}button.on.change{background:var(--chg);color:#111}
input.note{flex:1;min-width:220px;background:#121417;color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:6px 8px}
.saved{color:var(--dim);font-size:12px}
</style></head><body>
<header><h1>Odsłuch serii <span id="series"></span></h1>
<p>✓ przyjmij · ✗ odrzuć · ↻ zmień (napisz w notatce, co ma być inaczej). Każdy klik zapisuje feedback.json. Kliknij falę, by odtworzyć od tego miejsca.</p></header>
<main id="list"></main>
<script>
const SYMBOL = {accept: "✓ przyjmij", reject: "✗ odrzuć", change: "↻ zmień"};
async function save(stem, decision, note, card) {
  const r = await fetch("/feedback", {method: "POST", headers: {"Content-Type": "application/json"},
                                      body: JSON.stringify({clip: stem, decision, note})});
  const out = await r.json();
  card.querySelector(".saved").textContent = r.ok ? "zapisano " + new Date().toLocaleTimeString() : "błąd: " + out.error;
  if (r.ok) paint(card, decision);
}
function paint(card, decision) {
  card.className = "clip " + (decision || "");
  card.querySelectorAll("button[data-d]").forEach(b => b.classList.toggle("on", b.dataset.d === decision));
}
async function wave(canvas, url, audio) {
  const ctx = canvas.getContext("2d"), w = canvas.width = canvas.clientWidth * devicePixelRatio, h = canvas.height = 64 * devicePixelRatio;
  const data = await (await fetch(url)).arrayBuffer();
  const buffer = await new OfflineAudioContext(1, 1, 44100).decodeAudioData(data);
  const ch = [...Array(buffer.numberOfChannels).keys()].map(i => buffer.getChannelData(i));
  const per = Math.max(1, Math.floor(ch[0].length / w));
  ctx.fillStyle = "#7aa7d8";
  for (let x = 0; x < w; x++) {
    let lo = 0, hi = 0;
    for (const c of ch) for (let i = x * per; i < (x + 1) * per && i < c.length; i++) { lo = Math.min(lo, c[i]); hi = Math.max(hi, c[i]); }
    ctx.fillRect(x, (1 - hi) * h / 2, 1, Math.max(1, (hi - lo) * h / 2));
  }
  canvas.onclick = e => { audio.currentTime = buffer.duration * e.offsetX / canvas.clientWidth; audio.play(); };
}
function esc(s) { return String(s).replace(/[&<>"]/g, c => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"}[c])); }
(async () => {
  const {series, clips, feedback} = await (await fetch("/series")).json();
  document.getElementById("series").textContent = series;
  const list = document.getElementById("list");
  let sound = null;
  for (const c of clips) {
    if (c.sound !== sound) { sound = c.sound; list.insertAdjacentHTML("beforeend", `<h2>${esc(c.game_name)} (${esc(c.sound)})</h2>`); }
    const stem = c.file.slice(0, -4), m = c.checks.measures, f = c.checks.failures, v = feedback[stem] || {};
    const card = document.createElement("section");
    card.innerHTML = `<div class="top"><b>${esc(stem)}</b><span class="dim">seed ${c.seed} · ${c.steps} kroków · ${m.seconds} s · ${m.channels === 1 ? "mono" : "stereo"}${c.loop ? " · pętla" : ""}</span>
      <span class="${f.length ? "fail" : "pass"}">${f.length ? "✗ " + esc(f.join("; ")) : "✓ testy OK"}</span></div>
      <canvas></canvas>
      <div class="players"><span>gra:</span><audio controls preload="none" ${c.loop ? "loop" : ""} src="/audio/${encodeURIComponent(c.file)}"></audio>
      <span class="dim">surowy z modelu:</span><audio controls preload="none" src="/audio/${encodeURIComponent(c.raw)}"></audio></div>
      <div class="prompt">${esc(c.prompt)}</div>
      <div class="measures">${m.lufs} LUFS · ${m.true_peak_dbtp} dBTP · cisza na początku ${m.lead_silence} s${m.loop_seam_db !== undefined ? " · szew pętli " + m.loop_seam_db + " dB" : ""}</div>
      <div class="verdict">${Object.keys(SYMBOL).map(d => `<button data-d="${d}" class="${d}">${SYMBOL[d]}</button>`).join("")}
      <input class="note" placeholder="notatka (przy ↻ wymagana)" value="${esc(v.note || "")}"><span class="saved"></span></div>`;
    list.appendChild(card);
    paint(card, v.decision);
    const note = card.querySelector(".note");
    card.querySelectorAll("button[data-d]").forEach(b => b.onclick = () => save(stem, b.dataset.d, note.value, card));
    note.onchange = () => { const on = card.querySelector("button.on"); if (on) save(stem, on.dataset.d, note.value, card); };
    wave(card.querySelector("canvas"), `/audio/${encodeURIComponent(c.file)}`, card.querySelector("audio")).catch(() => {});
  }
})();
</script></body></html>
"""


def make_handler(series):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):  # quiet: the page is the interface
            pass

        def reply(self, status, body, content_type="application/json"):
            data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                return self.reply(HTTPStatus.OK, PAGE.encode("utf-8"), "text/html; charset=utf-8")
            if self.path == "/series":
                data = load_series(series)
                return self.reply(HTTPStatus.OK, {"series": series.name, "clips": data["clips"],
                                                  "feedback": load_feedback(series)["clips"]})
            if self.path.startswith("/audio/"):
                from urllib.parse import unquote
                name = unquote(self.path[len("/audio/"):])
                path = series / name
                # Only WAV files directly in the series folder.
                if "/" in name or "\\" in name or not name.endswith(".wav") or not path.is_file():
                    return self.reply(HTTPStatus.NOT_FOUND, {"error": "no such clip"})
                return self.reply(HTTPStatus.OK, path.read_bytes(), "audio/wav")
            return self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})

        def do_POST(self):
            if self.path != "/feedback":
                return self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(min(length, 65536)) or b"{}")
                set_verdict(series, str(body.get("clip", "")), body.get("decision"), body.get("note", ""))
            except (ValueError, json.JSONDecodeError) as error:
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            return self.reply(HTTPStatus.OK, {"ok": True})

    return Handler


def serve(series, port=8765, open_browser=True):
    load_series(series)  # fails early with a clear message
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(series))
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"Listening room for {series.name}: {url}  (Ctrl+C to stop; verdicts go to {feedback_path(series)})", flush=True)
    if open_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("series", help="folder under out/")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    series = OUT / args.series
    try:
        serve(series, args.port, not args.no_browser)
    except FileNotFoundError as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
