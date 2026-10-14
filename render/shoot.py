"""Serve the render folder on localhost and screenshot each view with headless Chromium."""
import sys, os, threading, functools, http.server, socketserver
from playwright.sync_api import sync_playwright

render_dir, outdir = sys.argv[1], sys.argv[2]
views = sys.argv[3:] or ["hero", "open", "cut"]
os.makedirs(outdir, exist_ok=True)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


socketserver.TCPServer.allow_reuse_address = True
httpd = socketserver.TCPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=render_dir))
port = httpd.server_address[1]
threading.Thread(target=httpd.serve_forever, daemon=True).start()

SIZES = {}
with sync_playwright() as p:
    b = p.chromium.launch(args=["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"])
    for v in views:
        w, h = SIZES.get(v, (1400, 1100))
        pg = b.new_page(viewport={"width": w, "height": h}, device_scale_factor=1.25)
        msgs = []
        pg.on("console", lambda m: msgs.append(m.text))
        pg.on("pageerror", lambda e: msgs.append("PAGEERROR " + str(e)))
        pg.goto(f"http://127.0.0.1:{port}/{os.environ.get('PAGE','index.html')}?view={v}")
        pg.wait_for_function("window.__done !== undefined", timeout=120000)
        state = pg.evaluate("window.__done")
        out = os.path.join(outdir, f"render_{v}.png")
        pg.screenshot(path=out)
        print(v, "->", out, "state:", state, "| console:", [m for m in msgs if "GPU stall" not in m][:6])
        pg.close()
    b.close()
httpd.shutdown()
