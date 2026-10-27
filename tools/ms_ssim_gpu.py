#!/usr/bin/env python3
"""
MS-SSIM on the AMD Instinct MI25 via fused-ssim (PyTorch MPS backend).

Drop-in replacement for the `-m` (MS-SSIM) mode of video-metric:

  * same decoding approach as src/pipe_decoder.c: local ffmpeg, raw yuv420p
    pipe, Y-plane only, uint8 -> [0,1]
  * same algorithm as src/msssim_core.c: 5-level Gaussian pyramid (level 0
    pre-blurred, then cascaded 2x2 box downsample), per-level full-SSIM
    means, weights {0.0448, 0.2856, 0.3001, 0.2363, 0.1333}, clamped-edge
    11x11 Gaussian window (sigma ~= 1.09)
  * every per-scale SSIM map is computed by fused-ssim's fused Metal kernel
    on the GPU (MPS binds the Instinct MI25 automatically)

Prints the same report line:
  MS-SSIM - Min: 0.996753, Max: 0.997313, Mean: 0.997075

Usage:
  python3 tools/ms_ssim_gpu.py -o original.mp4 -t test.mp4 [--batch 8] [--ffmpeg PATH]
"""

import argparse
import os
import subprocess
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F

try:
    import av as _av
    HAVE_PYAV = True
except ImportError:
    HAVE_PYAV = False

# ---------------------------------------------------------------------------
# Algorithm constants (must stay in sync with src/msssim_core.c)
# ---------------------------------------------------------------------------
GAUSS = np.array(
    [0.0102, 0.0278, 0.0656, 0.1353, 0.2421, 0.3679,
     0.2421, 0.1353, 0.0656, 0.0278, 0.0102], dtype=np.float32)
GAUSS = GAUSS / GAUSS.sum()

WEIGHTS = np.array([0.0448, 0.2856, 0.3001, 0.2363, 0.1333], dtype=np.float32)
NUM_LEVELS = 5
KERNEL_RADIUS = 5

C1 = 0.01 ** 2
C2 = 0.03 ** 2

MIN_DIM = 11 * (1 << (NUM_LEVELS - 1))  # 176 (same minimum as the C code)


# ---------------------------------------------------------------------------
# ffmpeg discovery / metadata (mirrors src/pipe_decoder.c)
# ---------------------------------------------------------------------------
def find_ffmpeg(explicit):
    if explicit:
        return explicit
    for p in ("./ffmpeg",
              os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "ffmpeg")):
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    # Convenience fallback not used by the C binary
    import shutil
    return shutil.which("ffmpeg")


def get_video_info_ffmpeg(ffmpeg, filepath):
    """Return dict(width, height, fps, num_frames) like get_video_info()."""
    proc = subprocess.run([ffmpeg, "-i", filepath],
                          capture_output=True, text=True)
    info = {"width": 0, "height": 0, "fps": 0.0, "num_frames": 0}
    total_duration = 0.0
    for line in ((proc.stdout or "") + (proc.stderr or "")).splitlines():
        if "Stream" in line and "Video" in line:
            for i, tok in enumerate(line.split()):
                if "x" in tok and tok.replace("x", "").isdigit():
                    w, h = tok.split("x")
                    info["width"], info["height"] = int(w), int(h)
                    break
            if "fps" in line:
                idx = line.index("fps")
                fps_tok = line[:idx].rstrip().split()
                try:
                    info["fps"] = float(fps_tok[-1].strip(","))
                except ValueError:
                    pass
            if info["fps"] > 0 and total_duration > 0:
                info["num_frames"] = int(total_duration * info["fps"] + 0.5)
        if "Duration:" in line:
            try:
                hh, mm, rest = line.split("Duration:")[1].split(":")[0:3]
                ss = float(rest.split(",")[0].strip())
                total_duration = int(hh) * 3600 + int(mm) * 60 + ss
                if info["fps"] > 0:
                    info["num_frames"] = int(total_duration * info["fps"] + 0.5)
            except (ValueError, IndexError):
                pass
    if info["num_frames"] == 0 and info["fps"] > 0 and total_duration > 0:
        info["num_frames"] = int(total_duration * info["fps"] + 0.5)
    return info


def get_video_info_pyav(filepath):
    """Metadata via in-process libav (no external ffmpeg needed)."""
    info = {"width": 0, "height": 0, "fps": 0.0, "num_frames": 0}
    try:
        c = _av.open(filepath)
        s = c.streams.video[0]
        cc = s.codec_context
        info["width"] = cc.width
        info["height"] = cc.height
        if s.average_rate and s.average_rate.denominator:
            info["fps"] = float(s.average_rate)
        try:
            if s.frames and s.frames > 0:
                info["num_frames"] = s.frames
        except ValueError:
            pass
        if info["num_frames"] == 0 and info["fps"] > 0 and c.duration:
            info["num_frames"] = int(c.duration / 1e6 * info["fps"] + 0.5)
        c.close()
    except Exception:
        pass
    return info


def get_video_info(ffmpeg, filepath):
    if HAVE_PYAV:
        return get_video_info_pyav(filepath)
    return get_video_info_ffmpeg(ffmpeg, filepath)


def frame_to_y(frame):
    """Extract the 8-bit Y plane exactly like `-pix_fmt yuv420p` + first W*H bytes.

    For native yuv420p this is a zero-copy view of the codec buffer, so the
    pixels are byte-identical to the pipe-based path.
    """
    if frame.format.name == "yuv420p":
        arr = frame.to_ndarray()  # (H*1.5, W) packed view, no swscale
        return np.ascontiguousarray(arr[:frame.height, :])
    arr = frame.to_ndarray(format="yuv420p")
    return np.ascontiguousarray(arr[:frame.height, :])


class AvDecoder:
    """In-process libav (PyAV) decoding of the two video streams."""

    def __init__(self, orig, test, threads=0):
        self.ref = _av.open(orig)
        self.dist = _av.open(test)
        for c in (self.ref, self.dist):
            cc = c.streams.video[0].codec_context
            # PyAV defaults to thread_type=SLICE, which leaves HEVC/AV1 decode
            # effectively single-threaded. FRAME|SLICE matches what the C
            # reference paths use and is ~8x faster at 4K.
            cc.thread_count = threads
            cc.thread_type = "AUTO"
        self.ref_iter = self.ref.decode(video=0)
        self.dist_iter = self.dist.decode(video=0)

    def read_pair(self):
        try:
            rf = next(self.ref_iter)
        except StopIteration:
            rf = None
        try:
            df = next(self.dist_iter)
        except StopIteration:
            df = None
        if rf is None and df is None:
            return None, None
        return (frame_to_y(rf) if rf is not None else None,
                frame_to_y(df) if df is not None else None)

    def close(self):
        for c in (self.ref, self.dist):
            try:
                c.close()
            except Exception:
                pass


def open_decode_pipe(ffmpeg, filepath):
    """Start ffmpeg decoding raw yuv420p to stdout (same command as C)."""
    return subprocess.Popen(
        [ffmpeg, "-i", filepath, "-f", "rawvideo", "-pix_fmt", "yuv420p", "pipe:1"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def read_y_plane(proc, total_bytes, npix):
    """Read one full YUV420 frame (blocking), return Y as uint8 array or None."""
    fd = proc.stdout.fileno()
    buf = bytearray()
    while len(buf) < total_bytes:
        chunk = os.read(fd, total_bytes - len(buf))
        if not chunk:
            return None
        buf += chunk
    return np.frombuffer(bytes(buf[:npix]), dtype=np.uint8).copy()


class PipeDecoder:
    """popen ffmpeg rawvideo yuv420p pipes (same command as the C binary)."""

    def __init__(self, ffmpeg, orig, test, width, height):
        self.ref = open_decode_pipe(ffmpeg, orig)
        self.dist = open_decode_pipe(ffmpeg, test)
        self.total = width * height * 3 // 2
        self.npix = width * height

    def read_pair(self):
        return (read_y_plane(self.ref, self.total, self.npix),
                read_y_plane(self.dist, self.total, self.npix))

    def close(self):
        for p in (self.ref, self.dist):
            try:
                p.stdout.close()
            except Exception:
                pass
            try:
                p.terminate()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# GPU/MS-SSIM core
# ---------------------------------------------------------------------------
import fused_ssim as _fs

fusedssim = getattr(_fs, "fusedssim", None)
if fusedssim is None:
    sys.exit("error: fused-ssim GPU backend was not loaded "
             "(this machine uses the MPS backend on the MI25)")

GAUSS_2D = None


def ensure_gpu():
    global GAUSS_2D
    if not torch.backends.mps.is_available():
        sys.exit("error: PyTorch MPS backend is not available")
    if GAUSS_2D is None:
        g = torch.tensor(GAUSS, dtype=torch.float32, device="mps")
        GAUSS_2D = g.view(1, 1, 11, 1) * g.view(1, 1, 1, 11)


def _blur(x):
    return F.conv2d(F.pad(x, (KERNEL_RADIUS,) * 4, mode="replicate"), GAUSS_2D)


def msssim_batch(ref_y, dist_y, height, width):
    """Compute MS-SSIM for a batch of Y frames [B,H,W] on the GPU.

    Returns per-frame MS-SSIM values (numpy float32, length B).
    """
    b = ref_y.shape[0]
    # Upload as uint8 (3.3 MB/frame at 4K) and convert to float32 on the GPU,
    # instead of converting on the CPU and uploading float32 (4x the bytes).
    ref = torch.from_numpy(ref_y.reshape(b, 1, height, width)).to(
        "mps", torch.uint8).float().div_(255.0)
    dist = torch.from_numpy(dist_y.reshape(b, 1, height, width)).to(
        "mps", torch.uint8).float().div_(255.0)

    # 5-level pyramid exactly like build_pyramid(): level0 pre-blurred,
    # then cascaded 2x2 box downsample of the previous (blurred) level.
    lv_r, lv_d = [_blur(ref)], [_blur(dist)]
    for _ in range(1, NUM_LEVELS):
        lv_r.append(F.avg_pool2d(lv_r[-1], 2, 2))
        lv_d.append(F.avg_pool2d(lv_d[-1], 2, 2))

    out = torch.ones(b, device="mps")
    for level in range(NUM_LEVELS):
        # Replicate-pad by 5 and use padding="valid" so the fused kernel sees
        # clamped edges (same boundary handling as msssim_core.c).
        pr = F.pad(lv_r[level], (KERNEL_RADIUS,) * 4, mode="replicate")
        pd_ = F.pad(lv_d[level], (KERNEL_RADIUS,) * 4, mode="replicate")
        ssim_map = fusedssim(C1, C2, pr, pd_, False)[0]
        ssim_map = ssim_map[:, :, KERNEL_RADIUS:-KERNEL_RADIUS,
                            KERNEL_RADIUS:-KERNEL_RADIUS]
        out = out * (ssim_map.mean(dim=(1, 2, 3)) ** float(WEIGHTS[level]))

    torch.mps.synchronize()
    return out.detach().cpu().numpy()


# ---------------------------------------------------------------------------
# Driver (mirrors compute_ms_ssim())
# ---------------------------------------------------------------------------
def progress_bar(percent):
    filled = max(0, min(percent, 100)) // 5
    bar = "#" * filled + "-" * (20 - filled)
    print(f"\r<{max(0, min(percent, 100)):3d}%>{bar} ", end="", flush=True)


def main():
    ap = argparse.ArgumentParser(description="MS-SSIM on the MI25 via fused-ssim")
    ap.add_argument("-o", "--output", required=True, help="reference video")
    ap.add_argument("-t", "--test", required=True, help="test video")
    ap.add_argument("--ffmpeg", default=None, help="ffmpeg binary (default: ./ffmpeg)")
    ap.add_argument("--batch", type=int, default=8, help="frames per GPU batch")
    ap.add_argument("--threads", type=int, default=0,
                    help="decode threads per stream (0=auto; only for --decoder pyav/auto)")
    ap.add_argument("--max-frames", type=int, default=0, help="stop after N frames (0=all)")
    ap.add_argument("--json", default=None, help="write per-frame values as JSON")
    ap.add_argument("--decoder", choices=["auto", "pyav", "pipe"], default="auto",
                    help="decode path (default auto: in-process PyAV if installed, "
                         "else ffmpeg pipes)")
    ap.add_argument("--pipeline", action="store_true",
                    help="decode in a background thread while the GPU computes "
                         "the previous batch (overlaps CPU decode with GPU work)")
    args = ap.parse_args()

    mode = args.decoder
    if mode == "pyav" and not HAVE_PYAV:
        print("error: --decoder pyav requested but PyAV is not installed",
              file=sys.stderr)
        return 1
    if mode == "pipe" and not find_ffmpeg(args.ffmpeg):
        print("error: local ffmpeg not found (place it next to the binary)",
              file=sys.stderr)
        return 1

    ffmpeg = find_ffmpeg(args.ffmpeg)
    ref_info = get_video_info(ffmpeg, args.output)
    dist_info = get_video_info(ffmpeg, args.test)
    if ref_info["width"] <= 0:
        print(f"error: failed to get metadata for reference video", file=sys.stderr)
        return 1
    if dist_info["width"] <= 0:
        print(f"error: failed to get metadata for test video", file=sys.stderr)
        return 1

    W, H = ref_info["width"], ref_info["height"]
    if W != dist_info["width"] or H != dist_info["height"]:
        print(f"error: resolution mismatch ({W}x{H} vs "
              f"{dist_info['width']}x{dist_info['height']})", file=sys.stderr)
        return 1
    if W < MIN_DIM or H < MIN_DIM:
        print(f"error: resolution too small ({W}x{H}, minimum {MIN_DIM}x{MIN_DIM})",
              file=sys.stderr)
        return 1

    expected = ref_info["num_frames"] or 1000

    ensure_gpu()

    print("Computing MS-SSIM...")
    if HAVE_PYAV and mode != "pipe":
        decoder = AvDecoder(args.output, args.test, args.threads)
    else:
        decoder = PipeDecoder(ffmpeg, args.output, args.test, W, H)

    values = []
    t0 = time.time()

    def fail_context(stage):
        """Report where a long run stopped: frame index and memory state."""
        try:
            import resource
            rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024 * 1024)
        except Exception:
            rss = float("nan")
        mps = ""
        try:
            mps = (f", mps_alloc={torch.mps.current_allocated_memory() / 2**20:.0f}MB"
                   f", mps_driver={torch.mps.driver_allocated_memory() / 2**20:.0f}MB")
        except Exception:
            pass
        print(f"\nerror in {stage} at frame {len(values)} of {expected} "
              f"(rss={rss:.0f}MB{mps})", file=sys.stderr)

    def flush(batch_r, batch_d):
        if len(batch_r) == 0:
            return
        vals = msssim_batch(np.stack(batch_r), np.stack(batch_d), H, W)
        values.extend(float(v) for v in vals)

    if not args.pipeline:
        # Synchronous: decode a frame pair, then compute.
        batch_r, batch_d = [], []
        n_frames = 0
        try:
            while True:
                ry, dy = decoder.read_pair()
                if ry is None and dy is None:
                    break
                if ry is None or dy is None:
                    print(f"\nWarning: frame count mismatch between videos", file=sys.stderr)
                    break

                batch_r.append(ry)
                batch_d.append(dy)
                n_frames = len(values) + len(batch_r)
                if len(batch_r) >= args.batch or (args.max_frames and n_frames >= args.max_frames):
                    flush(batch_r, batch_d)
                    batch_r, batch_d = [], []
                    if expected > 0:
                        progress_bar(int(min(n_frames, expected) * 100 / expected))
                    else:
                        print(f"\rprocessed {n_frames} frames", end="", flush=True)
                    if args.max_frames and len(values) >= args.max_frames:
                        break
        except Exception:
            fail_context("decode/compute")
            raise
        finally:
            flush(batch_r, batch_d)
            decoder.close()
    else:
        # Pipelined: a producer thread decodes the next batch while the GPU
        # computes the current one. Throughput approaches max(decode, GPU).
        import queue as _queue
        from threading import Thread

        q = _queue.Queue(maxsize=2)
        state = {"mismatch": False, "error": None, "n": 0}

        def producer():
            br, bd = [], []
            try:
                while True:
                    ry, dy = decoder.read_pair()
                    if ry is None and dy is None:
                        break
                    if ry is None or dy is None:
                        state["mismatch"] = True
                        break
                    br.append(ry)
                    bd.append(dy)
                    if len(br) >= args.batch or (args.max_frames and state["n"] + len(br) >= args.max_frames):
                        q.put([np.stack(br), np.stack(bd)])
                        state["n"] += len(br)
                        br, bd = [], []
                    if args.max_frames and state["n"] >= args.max_frames:
                        break
            except Exception as e:                      # noqa: BLE001
                state["error"] = e
            finally:
                if br:
                    try:
                        q.put([np.stack(br), np.stack(bd)])
                    except Exception:
                        pass
                q.put(None)

        thread = Thread(target=producer, daemon=True)
        thread.start()
        try:
            while True:
                item = q.get()
                if item is None:
                    break
                flush(item[0], item[1])
                n_frames = len(values)
                if expected > 0:
                    progress_bar(int(min(n_frames, expected) * 100 / expected))
                else:
                    print(f"\rprocessed {n_frames} frames", end="", flush=True)
            thread.join()
        except Exception:
            fail_context("GPU compute (pipeline)")
            raise
        finally:
            decoder.close()
        if state["error"]:
            fail_context("decode (producer thread)")
            raise state["error"]
        if state["mismatch"]:
            print("\nWarning: frame count mismatch between videos", file=sys.stderr)

    if args.max_frames:
        values = values[:args.max_frames]

    if not values:
        print("error: no frames processed", file=sys.stderr)
        return 1

    if args.json:
        import json
        with open(args.json, "w") as f:
            json.dump({"width": W, "height": H, "values": values}, f, indent=1)

    els = time.time() - t0
    print("\r<100%>#################### ")
    print(f"MS-SSIM - Min: {min(values):.6f}, Max: {max(values):.6f}, "
          f"Mean: {sum(values) / len(values):.6f}")
    print(f"(processed {len(values)} frames in {els:.1f}s, "
          f"{els / max(1, len(values)) * 1000:.1f} ms/frame)")
    return 0


if __name__ == "__main__":
    sys.exit(main())