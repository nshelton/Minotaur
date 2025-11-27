
# %%
import cv2
import matplotlib.pyplot as plt
import numpy as np

# ----------------------------
# Color space helpers
# ----------------------------
def srgb_to_linear(x):
    """x: float array in [0,1] sRGB -> linear RGB"""
    a = 0.055
    return np.where(x <= 0.04045, x / 12.92, ((x + a) / (1 + a)) ** 2.4)

def linear_to_srgb(x):
    """linear RGB in [0,1] -> sRGB"""
    a = 0.055
    return np.where(x <= 0.0031308, 12.92 * x, (1 + a) * (x ** (1/2.4)) - a)

def boost_saturation_srgb(rgb, sat_gain=1.4):
    """
    Optional: saturate the image a bit (in HSV) BEFORE basis estimation.
    This is a heuristic; do unmixing in linear RGB afterward.
    """
    hsv = cv2.cvtColor((np.clip(rgb, 0, 1) * 255).astype(np.uint8), cv2.COLOR_RGB2HSV)
    h, s, v = cv2.split(hsv)
    s = np.clip(s.astype(np.float32) * sat_gain, 0, 255).astype(np.uint8)
    hsv2 = cv2.merge([h, s, v])
    rgb2 = cv2.cvtColor(hsv2, cv2.COLOR_HSV2RGB).astype(np.float32) / 255.0
    return rgb2

# ----------------------------
# Basis estimation
# ----------------------------
def estimate_rgb_bases(rgb_lin, frac=0.01, eps=1e-8):
    """
    Estimate imperfect R/G/B basis colors from the image itself by picking
    the most "red-dominant", "green-dominant", "blue-dominant" pixels.

    rgb_lin: linear RGB float image in [0,1], shape (H,W,3)
    frac: top fraction of dominant pixels to use per channel
    """
    H, W, _ = rgb_lin.shape
    flat = rgb_lin.reshape(-1, 3)

    R, G, B = flat[:, 0], flat[:, 1], flat[:, 2]
    # dominance scores (how much more of this channel than the others)
    sR = R - np.maximum(G, B)
    sG = G - np.maximum(R, B)
    sB = B - np.maximum(R, G)

    def robust_pick(score):
        k = max(50, int(frac * flat.shape[0]))
        idx = np.argpartition(score, -k)[-k:]
        # robust central tendency (median is good against outliers)
        v = np.median(flat[idx], axis=0)
        return np.clip(v, eps, 1.0)

    bR = robust_pick(sR)
    bG = robust_pick(sG)
    bB = robust_pick(sB)

    # Make sure columns are not nearly collinear
    M = np.stack([bR, bG, bB], axis=1)  # 3x3, columns are basis colors
    return M

# ----------------------------
# Unmixing
# ----------------------------
def unmix_rgb_layers(
    bgr_u8,
    sat_gain_for_basis=1.0,   # set >1.0 if your bases are dull and you want a heuristic boost
    basis_frac=0.01,
    ridge=1e-4,               # regularization strength (stabilizes inversion)
    clamp01=True,
    threshold=0.25,           # mask threshold on weights
    morph=True
):
    """
    Returns:
      weights: (H,W,3) float weights for [R,G,B] layers
      masks:   (H,W,3) uint8 masks (0/255)
      bases:   3x3 basis matrix in linear RGB (columns = estimated R,G,B colors)
    """
    # Convert to RGB float in [0,1]
    rgb = cv2.cvtColor(bgr_u8, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0

    # Optional heuristic saturation boost for basis estimation only
    rgb_for_basis = boost_saturation_srgb(rgb, sat_gain_for_basis) if sat_gain_for_basis != 1.0 else rgb

    # Linearize
    rgb_lin = srgb_to_linear(rgb)
    rgb_lin_for_basis = srgb_to_linear(rgb_for_basis)

    # Estimate basis colors (in linear RGB)
    M = estimate_rgb_bases(rgb_lin_for_basis, frac=basis_frac)  # 3x3

    # Precompute regularized least-squares operator:
    # a = argmin ||M a - c||^2 + ridge*||a||^2  => a = (M^T M + ridge I)^-1 M^T c
    MtM = M.T @ M
    Aop = np.linalg.inv(MtM + ridge * np.eye(3, dtype=np.float32)) @ M.T  # 3x3

    # Apply to all pixels (vectorized)
    H, W, _ = rgb_lin.shape
    C = rgb_lin.reshape(-1, 3).T  # 3xN
    weights = (Aop @ C).T.reshape(H, W, 3)  # HxWx3

    # Nonnegativity + optional [0,1] clamp
    weights = np.maximum(weights, 0.0)
    if clamp01:
        weights = np.minimum(weights, 1.0)

    # Hard masks
    masks = (weights > threshold).astype(np.uint8) * 255

    # Optional cleanup
    if morph:
        k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        for i in range(3):
            masks[..., i] = cv2.morphologyEx(masks[..., i], cv2.MORPH_OPEN, k, iterations=1)
            masks[..., i] = cv2.morphologyEx(masks[..., i], cv2.MORPH_CLOSE, k, iterations=1)

    return weights, masks, M

# ----------------------------
# Example usage
# ----------------------------
 
inputfile = "c:/Users/nick/Pictures/4004-masks-composite.jpg"

img = cv2.imread(inputfile)
img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 255
img = 1 - np.pow(img, 2) 

img_subset = img[400:600, 500:700]




mask = np.linalg.norm(img_subset, axis = 2) < 1.6

# img_subset = img_subset/ np.linalg.norm(img_subset, axis=2)[...,np.newaxis]

plt.matshow(img_subset)
plt.show()

# r = (img_subset[...,2])  + (img_subset[...,0]) > 1.2
g = img_subset[...,1] > 0.5

cv2.imwrite("mask_G.png",(g * 255).astype(np.uint8))
# plt.matshow(r)
plt.matshow(g)
plt.show()





# weights, masks, bases = unmix_rgb_layers(
#     img_subset,
#     sat_gain_for_basis=1,   # try 1.0, 1.2, 1.4
#     basis_frac=0.01,
#     ridge=1e-4,
#     threshold=0.25,
#     morph=True
# )

# plt.matshow(masks)
# plt.show()
# # Save masks
# cv2.imwrite("mask_R.png", masks[..., 0])
# cv2.imwrite("mask_G.png", masks[..., 1])
# cv2.imwrite("mask_B.png", masks[..., 2])

# # Also save soft weights visualization (optional)
# w_vis = (np.clip(weights, 0, 1) * 255).astype(np.uint8)
# cv2.imwrite("weights_rgb.png", cv2.cvtColor(w_vis, cv2.COLOR_RGB2BGR))
