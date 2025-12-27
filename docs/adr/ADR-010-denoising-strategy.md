# ADR-010: Denoising Strategy

Status: Proposed
Date: 2025-12-26
Deciders: @aaron

## Context

Path tracing is inherently noisy at low sample counts. Currently we rely on temporal accumulation (progressive refinement), which works well for static scenes but:

- Requires many frames to converge to a clean image
- Resets on any camera motion, causing visible noise during interaction
- Doesn't help with real-time preview or animation

As scene complexity grows (more CSG operations, Bezier patches), sample counts per frame may decrease, making noise more prominent.

## Options Considered

### Option 1: A-Trous Wavelet Filter (Spatial Only)

The edge-avoiding À-Trous wavelet transform (Dammertz et al. 2010):

- **How it works**: Multi-pass spatial filter with increasing step sizes (1, 2, 4, 8, 16 pixels). Uses G-buffer data (normals, depth) for edge-stopping functions to preserve sharp boundaries.
- **Pros**: Simple to implement, fast, predictable results, no temporal artifacts
- **Cons**: Spatial only (no temporal coherence), can over-blur fine details

### Option 2: SVGF (Spatiotemporal Variance-Guided Filtering)

State-of-the-art real-time denoising (Schied et al. 2017):

- **How it works**: Combines temporal reprojection with spatial A-Trous filtering. Uses variance estimation to guide filter strength.
- **Pros**: Excellent quality, handles motion well, industry standard for real-time RT
- **Cons**: Complex implementation, requires motion vectors, temporal ghosting possible

### Option 3: AI Denoiser (OIDN / OptiX)

Machine learning based denoising:

- **How it works**: Neural network trained on path-traced images, runs as post-process
- **Pros**: Best quality, handles difficult cases (caustics, fine details)
- **Cons**: External dependency, specific GPU requirements, latency, binary blob

### Option 4: More Samples (No Denoiser)

Brute force approach:

- **How it works**: Just render more samples per pixel
- **Pros**: Ground truth quality, no artifacts, simplest code
- **Cons**: Slower convergence, doesn't help interactive use case

## Implementation Considerations

### G-Buffer Requirements

Most denoising approaches need auxiliary buffers:
- World-space normals (for edge detection)
- Depth/position (for edge detection)
- Albedo (for demodulation/remodulation)
- Motion vectors (for temporal approaches)

These could be:
1. Packed into existing output (e.g., use alpha channel, separate render targets)
2. Written in a separate G-buffer pass
3. Computed on-demand during denoising (expensive)

### Integration Points

- **Compile-time**: Feature flag like `FEATURE_DENOISE`
- **Runtime**: Toggle in UI, adjustable quality settings
- **Pipeline**: Additional compute dispatch(es) after main ray trace

### Performance Budget

Current frame times at 800x600:
- ~11ms per frame (93 fps)
- Denoiser should add <5ms to remain interactive

## Decision

**Deferred.** Capture as future work. When implementing:

1. Start with A-Trous (Option 1) as baseline - simpler, no temporal state
2. Add G-buffer output to main shader (normals, depth)
3. Implement as separate compute shader pass
4. Consider SVGF (Option 2) if temporal stability becomes important

## Consequences

### If We Implement A-Trous

**Positive:**
- Cleaner images during camera motion
- Lower sample counts viable for preview
- Foundation for more advanced techniques

**Negative:**
- Additional GPU memory for G-buffer
- Extra compute passes per frame
- Potential for over-blurred results if not tuned well

**Neutral:**
- Need to decide on quality/performance tradeoff (number of passes)
- May want runtime toggle for A/B comparison

## References

- Dammertz et al. 2010: "Edge-Avoiding À-Trous Wavelet Transform for Fast Global Illumination Filtering"
- Schied et al. 2017: "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for Path-Traced Global Illumination"
- Intel OIDN: https://www.openimagedenoise.org/
