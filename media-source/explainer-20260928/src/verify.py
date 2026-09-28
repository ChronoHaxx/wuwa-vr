"""Verify the rendered MP4, build the contact sheet from decoded frames, write receipt.json."""
import hashlib, json, os, subprocess
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.normpath(os.path.join(HERE, "..", "out"))
B = os.environ.get("FFBIN", "").rstrip("/\\") + "/" if os.environ.get("FFBIN") else ""; NW = 0x08000000
MP4 = os.path.join(OUT, "WuWa-VR-Explained-60s.mp4")
run = lambda c: subprocess.run(c, capture_output=True, text=True, creationflags=NW)
probe = json.loads(run([B + "ffprobe.exe", "-v", "error", "-show_format", "-show_streams", "-count_frames", "-of", "json", MP4]).stdout)
dec = run([B + "ffmpeg.exe", "-v", "error", "-xerror", "-i", MP4, "-f", "null", "-"])
head = open(MP4, "rb").read(4096)
moov, mdat = head.find(b"moov"), head.find(b"mdat")
times = [2.0, 6.5, 10.5, 18.5, 23.0, 29.0, 34.0, 39.0, 43.5, 48.0, 54.0, 58.5]
f = ImageFont.truetype("C:/Windows/Fonts/seguisb.ttf", 16)
sheet = Image.new("RGB", (4 * 320, 3 * 206), (0, 0, 0)); d = ImageDraw.Draw(sheet)
for i, t in enumerate(times):
    raw = subprocess.run([B + "ffmpeg.exe", "-v", "error", "-ss", str(t), "-i", MP4, "-frames:v", "1", "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-"], capture_output=True, creationflags=NW).stdout
    im = Image.frombytes("RGB", (1280, 720), raw).resize((320, 180), Image.LANCZOS)
    x, y = (i % 4) * 320, (i // 4) * 206
    sheet.paste(im, (x, y)); d.text((x + 6, y + 182), f"{t:.1f}s", font=f, fill=(230, 230, 230))
sheet.save(os.path.join(OUT, "contact-sheet.png"))
v = [s for s in probe["streams"] if s["codec_type"] == "video"][0]
rec = {
    "file": "WuWa-VR-Explained-60s.mp4",
    "sha256": hashlib.sha256(open(MP4, "rb").read()).hexdigest(),
    "size_bytes": os.path.getsize(MP4),
    "container_duration_s": float(probe["format"]["duration"]),
    "codec": v["codec_name"], "profile": v.get("profile"), "pix_fmt": v["pix_fmt"],
    "width": v["width"], "height": v["height"], "r_frame_rate": v["r_frame_rate"],
    "nb_read_frames": int(v["nb_read_frames"]),
    "audio_streams": sum(1 for s in probe["streams"] if s["codec_type"] == "audio"),
    "faststart_moov_before_mdat": moov != -1 and (mdat == -1 or moov < mdat),
    "full_decode_exit_code": dec.returncode, "full_decode_stderr": dec.stderr.strip(),
    "contact_sheet_times_s": times,
    "audio_status": "silent: no audio track; captions burned in and in captions.srt",
    "footage": "only site/media/feature-first-person.mp4 (first 6 s, read-only), labelled EARLIER-BUILD FOOTAGE at 20-32 s",
}
json.dump(rec, open(os.path.join(OUT, "receipt.json"), "w"), indent=2)
print(json.dumps(rec, indent=2))
