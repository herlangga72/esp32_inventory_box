#!/usr/bin/env python3
"""Minify JS in app.js and inline scripts in index.html using terser."""
import re, subprocess, os, tempfile, shutil

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
INDEX = os.path.join(DIR, "index.html")
APPJS = os.path.join(DIR, "app.js")
APPJS_OUT = os.path.join(DIR, "app.js")

def terser(code, label="js", toplevel=False):
    """Run terser via npx, return minified code."""
    cmd = ["npx", "--yes", "terser", "--compress", "--mangle"]
    if toplevel:
        cmd.append("--toplevel")
    try:
        r = subprocess.run(cmd, input=code, capture_output=True, text=True, timeout=30)
        if r.returncode != 0:
            print(f"  terser {label}: {r.stderr.strip()}")
            return code
        return r.stdout.strip()
    except Exception as e:
        print(f"  terser {label}: {e} — using original")
        return code

# ---- Minify app.js ----
with open(APPJS) as f:
    app_js = f.read()
print(f"app.js: {len(app_js)} bytes → ", end="", flush=True)
app_min = terser(app_js, "app.js", toplevel=False)
with open(APPJS_OUT, "w") as f:
    f.write(app_min)
print(f"{len(app_min)} bytes")

# ---- Minify inline scripts in index.html ----
with open(INDEX) as f:
    html = f.read()

def minify_script(m):
    code = m.group(1).strip()
    if not code:
        return m.group(0)
    short = terser(code, "inline", toplevel=False)
    return f"<script>{short}</script>"

html_new = re.sub(r'<script>(.*?)</script>', minify_script, html, flags=re.DOTALL)
print(f"index.html: {len(html)} bytes → {len(html_new)} bytes")

with open(INDEX, "w") as f:
    f.write(html_new)
print("Done")
