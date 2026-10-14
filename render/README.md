# Renders

How the pictures in `docs/renders` and `docs/assembly` are made.

```sh
cad/build.sh path/to/original.3mf            # builds build/boxframe/
python3 render/export.py build/boxframe render/scene.glb
cd render && npm install three@0.160.0 && cd ..
python3 -m pip install playwright            # uses Chromium
python3 render/shoot.py render out hero open back s1 s2 s3 s4 s5 s6 s7 s8 s9
```

Views and step captions are defined in the `V` table in `index.html`.
