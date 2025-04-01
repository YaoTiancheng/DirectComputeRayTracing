# DirectComputeRayTracing
Unidirectional MC path tracer using D3D12 compute shader.

## Features
- Two level BVH acceleration structure
- Point light, directional light, environment light and triangle light
- Image based lighting
- Multiple importance sampling
- Pinhole and thin-lens camera model
- Adjustable bokeh shape
- Physically based exposure
- Physically based BRDFs and BSDFs
- Kulla-Conty multiscattering approximation
- Both megakernel and wavefront implementations
- Loading scene from obj file and partial support of mistuba 3.0 scene format

## Screen shots
![Coffee](https://onedrive.live.com/embed?resid=46FF59C600EB91ED%213291&authkey=%21APZWQq3g0SL42sI&width=2562&height=1453)
![Spaceship](https://onedrive.live.com/embed?resid=46FF59C600EB91ED%213293&authkey=%21ACuUZFtIbRN51wM&width=3840&height=2088)
![Lamp](https://onedrive.live.com/embed?resid=46FF59C600EB91ED%213292&authkey=%21AKO8mMGMpviIsv8&width=2562&height=1453)

## Build
- Visual Studio 2017 (v141)
- Windows SDK version 10.0.19041.0

## References
- pbrt-v3 https://github.com/mmp/pbrt-v3.git
- mitsuba https://github.com/mitsuba-renderer/mitsuba.git
- Falcor https://github.com/NVIDIAGameWorks/Falcor.git
- [Revisiting Physically Based Shading at Imageworks](https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf)
- GPU-Raytracer https://github.com/jan-van-bergen/GPU-Raytracer.git

## Credits
This renderer is built with these software:
- tinyobjloader https://github.com/tinyobjloader/tinyobjloader.git
- RapidXML
- MikkTSpace http://www.mikktspace.com/
- imgui https://github.com/ocornut/imgui.git

