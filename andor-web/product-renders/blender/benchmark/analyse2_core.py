import numpy as np
def shift_est(a, b):
    A, B = np.fft.fft2(a), np.fft.fft2(b); R = A * np.conj(B); R /= np.abs(R) + 1e-9
    r = np.fft.ifft2(R).real; y, x = np.unravel_index(np.argmax(r), r.shape); H, W = r.shape
    def sub(c, m, n):  # parabolic peak refinement
        l, rr = r[(c[0] - (n == 0)) % H, (c[1] - (n == 1)) % W], r[(c[0] + (n == 0)) % H, (c[1] + (n == 1)) % W]
        d = l - 2 * r[c] + rr; return 0.0 if d == 0 else 0.5 * (l - rr) / d
    dy = y + sub((y, x), H, 0); dx = x + sub((y, x), W, 1)
    if dy > H / 2: dy -= H
    if dx > W / 2: dx -= W
    return dy, dx
def fshift(img, dy, dx):
    H, W = img.shape; ky = np.fft.fftfreq(H)[:, None]; kx = np.fft.fftfreq(W)[None, :]
    return np.fft.ifft2(np.fft.fft2(img) * np.exp(-2j * np.pi * (ky * dy + kx * dx))).real
