

## Feature Backlog

### Rendering / UI
- [ ] Draw bitmap and paths together when a bitmap entity has path-producing filters
  (per-layer visibility, different colors per layer)
- [ ] Live plot progress overlay - change line color blue->orange as paths are plotted

### Performance
- [ ] Audit memory usage in filter chain

### Filters
- [ ] Line displace filter (needs winding mode for closed paths to determine normal direction)
- [ ] Blur has artifacts if radius < 1

### New Layer Types
- [ ] "Field" layer type for scalar/vector fields from raster or paths
- [ ] Field-based line extraction filters
- [ ] Quiver plot renderer for vector fields

## Bugs

