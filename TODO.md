
## Rendering/ UI
- [ ] Draw bitmap and paths together, if theres a bitmap that gets converted to paths (Maybe even draw all layers in different colors?? per-layer visibility)
- [ ] centerline trace (currently hangs, need to understand this better)

## Performance

- [ ] use kdtree for the path optimisation, maybe start unit tests for this
- [ ] Audit memory usage patterns in filter chain

## Filters

- [ ] erode / dilate
- [ ] line displace
    This one is tricky, can estimate normal based on line direction, move line in normal direction
    but, we don't know which way the line is oriented, this could erode or dilate line
- we would need to add winding mode for closed paths, that could work

## "Field" layer type
- need a new render type "field" which can produce a scalar or vector field from a raster / paths ?
- can implement different field filters,a nd line extractions from fields ? need to think about this some more

How to render vector field ? quiver ? could be fun