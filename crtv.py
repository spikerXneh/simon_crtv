import io
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

WIDTH, HEIGHT = 480, 320
FPS = 20
SECONDS = 12
TOTAL_FRAMES = FPS * SECONDS
out_path = "output7.mjpeg"

# 1. Strong horizontal CRT scanlines
scanlines = np.ones((HEIGHT, 1), dtype=np.float32)
scanlines[1::2] = 0.22

# 2. Heavy CRT corner vignette
y_idx, x_idx = np.ogrid[:HEIGHT, :WIDTH]
norm_x = (x_idx - WIDTH / 2) / (WIDTH / 2)
norm_y = (y_idx - HEIGHT / 2) / (HEIGHT / 2)
r2 = norm_x**2 + norm_y**2
vignette = np.clip(1.0 - 0.55 * (r2**1.3), 0.12, 1.0).astype(np.float32)

# Precompute coordinate grids for diagonal RF interference waves
X, Y = np.meshgrid(np.arange(WIDTH, dtype=np.float32), np.arange(HEIGHT, dtype=np.float32))

# --- Helper to Generate Distorted Subliminal Ghost Masks ---
def create_ghost_mask(kind="figure", offset_x=0):
    img = Image.new("L", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(img)
    cx = WIDTH // 2 + offset_x

    if kind == "figure":
        # Looming shadowy figure standing in the static
        # Head + neck
        draw.ellipse([cx - 26, 48, cx + 26, 112], fill=255)
        draw.rectangle([cx - 12, 105, cx + 12, 132], fill=255)
        # Broad hunched shoulders & torso
        draw.polygon([
            (cx - 72, 145), (cx + 72, 145),
            (cx + 52, 320), (cx - 52, 320)
        ], fill=255)
        draw.ellipse([cx - 78, 130, cx + 78, 185], fill=255)
        # Faint glowing pinprick eyes inside the dark head
        draw.ellipse([cx - 12, 74, cx - 7, 79], fill=40)
        draw.ellipse([cx + 7, 74, cx + 12, 79], fill=40)
    else:
        # Close-up gaunt face peering out of the static
        draw.ellipse([cx - 68, 35, cx + 68, 235], fill=220)
        # Hollow dark eye sockets
        draw.ellipse([cx - 42, 95, cx - 12, 132], fill=20)
        draw.ellipse([cx + 12, 95, cx + 42, 132], fill=20)
        # Gaunt hollow mouth / shadow
        draw.ellipse([cx - 18, 168, cx + 18, 208], fill=25)

    # Blur slightly so edges dissolve organically into the CRT static
    img = img.filter(ImageFilter.GaussianBlur(radius=4.5))
    return np.asarray(img, dtype=np.float32) / 255.0

# Schedule 4 brief subliminal ghost bursts across the 240-frame (12s) loop
# Format: {start_frame: (duration_frames, ghost_type, x_offset, polarity)}
# polarity = -1 carves a dark shadow out of bright static; +1 burns a bright crimson ghost
ghost_schedule = {
    38:  (3, "figure", -45, -1),
    96:  (2, "face",    30,  1),
    158: (4, "figure",  20, -1),
    212: (2, "face",   -25, -1),
}

active_ghost = None
ghost_frames_left = 0

prev_intensity = np.zeros((HEIGHT, WIDTH), dtype=np.float32)

with open(out_path, "wb") as f:
    for frame_idx in range(TOTAL_FRAMES):
        # Check if a ghost flash starts on this frame
        if frame_idx in ghost_schedule:
            dur, g_type, g_off, g_pol = ghost_schedule[frame_idx]
            active_ghost = (create_ghost_mask(g_type, g_off), g_pol)
            ghost_frames_left = dur

        # A. Base fine snow + horizontal analog beam smear
        raw_noise = np.random.normal(0.32, 0.36, (HEIGHT, WIDTH)).astype(np.float32)
        smeared_noise = (
            0.36 * raw_noise +
            0.24 * np.roll(raw_noise, 1, axis=1) +
            0.18 * np.roll(raw_noise, 2, axis=1) +
            0.12 * np.roll(raw_noise, -1, axis=1) +
            0.10 * np.roll(raw_noise, 3, axis=1)
        )

        # B. Per-line horizontal gain jitter
        line_jitter = np.random.normal(1.0, 0.22, (HEIGHT, 1)).astype(np.float32)
        static = smeared_noise * line_jitter

        # C. Diagonal RF herringbone interference wave
        phase = frame_idx * 0.45
        rf_wave = 0.14 * np.sin(0.18 * X + 0.42 * Y + phase) + 0.08 * np.sin(-0.09 * X + 0.65 * Y - phase * 1.3)
        static += rf_wave

        # D. Slow rolling analog hum bar
        hum_y = (frame_idx * 9) % HEIGHT
        dist_to_hum = np.abs(((Y - hum_y + HEIGHT / 2) % HEIGHT) - HEIGHT / 2)
        hum_bar = 0.28 * np.exp(-(dist_to_hum**2) / (2 * 18.0**2))
        static += hum_bar

        # E. Blend active ghost apparition into the RF signal BEFORE sync-tearing
        if ghost_frames_left > 0 and active_ghost is not None:
            g_mask, g_pol = active_ghost
            # Jitter the ghost horizontally each frame so it feels unstable
            jittered_mask = np.roll(g_mask, np.random.randint(-12, 13), axis=1)
            if g_pol < 0:
                # Carve a dark shadow silhouette into heightened static
                static = static * (1.0 - 0.82 * jittered_mask) + 0.22
            else:
                # Burn a bright crimson apparition through the static
                static = static + 0.55 * jittered_mask
            ghost_frames_left -= 1

        # F. Horizontal sync-tear & bright static bursts (stronger during ghost flashes)
        tear_chance = 0.90 if ghost_frames_left > 0 else 0.55
        if np.random.rand() < tear_chance:
            for _ in range(np.random.randint(1, 4)):
                tear_y = np.random.randint(8, HEIGHT - 20)
                tear_h = np.random.randint(2, 12)
                shift = np.random.randint(-55, 56)
                static[tear_y:tear_y + tear_h] = np.roll(static[tear_y:tear_y + tear_h], shift, axis=1)
                if np.random.rand() < 0.4:
                    static[tear_y:tear_y + tear_h] += 0.35

        # G. Crush blacks & boost crimson contrast
        static = np.clip((static - 0.34) * 2.6, 0.0, 1.0)

        # H. CRT phosphor persistence (lingers slightly after ghost disappears)
        intensity = np.maximum(static, prev_intensity * 0.34)
        prev_intensity = intensity.copy()

        # Apply scanlines and vignette
        final_val = intensity * scanlines * vignette

        # I. Map to deep blood-red CRT phosphor palette
        frame = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)
        frame[:, :, 0] = np.clip(final_val * 245, 0, 255).astype(np.uint8)
        frame[:, :, 1] = np.clip((final_val**4) * 45, 0, 255).astype(np.uint8)
        frame[:, :, 2] = np.clip((final_val**5) * 25, 0, 255).astype(np.uint8)

        # J. Encode JPEG at quality 62 (guaranteed < 64 KB)
        buf = io.BytesIO()
        Image.fromarray(frame, mode="RGB").save(buf, format="JPEG", quality=62)
        jpg_bytes = buf.getvalue()

        if len(jpg_bytes) >= 65000:
            buf = io.BytesIO()
            Image.fromarray(frame, mode="RGB").save(buf, format="JPEG", quality=45)
            jpg_bytes = buf.getvalue()

        f.write(jpg_bytes)

print(f"Done! Saved {out_path} ({TOTAL_FRAMES} frames) with subliminal ghost flashes.")
