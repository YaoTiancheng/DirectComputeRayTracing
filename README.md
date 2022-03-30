# DirectComputeRayTracing
WIP Mega-kernel unidirectional Monte Carlo progressive path tracer based on D3D11 compute shader.

## Features
- Multiple importance sampling
- Point light, rectangle light and image based lighting
- BVH acceleration structure
- Loading Wavefront OBJ file
- MikkTSpace tangent vector generation
- Simple auto exposure and tonemapping
- Dielectric, conductor and transmissive BxDFs
- Energy conserving microfacet BRDF and BSDF

## Build
- Visual Studio 2017 (v141)
- Windows SDK version 10.0.19041.0

## Example Renderings
![Simple Geometries](https://dsm01pap001files.storage.live.com/y4mlJsNQdGHKksgH59ruV2iGbA70CVJynMqsoEmVp_lr1OKiXhNv50e6y9a64z7Kr_pbICNwaJzox2ojXD7ctHp7rkS3iKz6AIjwM3-ZqXTSs4WsK0zShiXwpHPWFuqvuzLJiRi4YWwKoodG4DUIYkigdr4W0wCan3JKuH_F1a9NnFs33_-4orLmBRwERXhKDzR?width=3840&height=2100&cropmode=none)
![Simple Geometries](https://dsm01pap001files.storage.live.com/y4mW0646RrUbd1AvwkO3ArPSH3oe624S-OVH9E5pUW8VS7H00Xnl9fQvRyosVjsq4MpO3gbHfkzIHnhWEwzTGuVvs5OQnE82lBggXLmFGoKSO16P10JkMPp-pAx4WiMlMHiyi0Ek0G5t-QGeTM6h053dpcqwF4j-nZmir2N7yFVsXZSyce9wdAiZ5mee0p1kNVL?width=3840&height=2100&cropmode=none)
![Simple Geometries](https://dsm01pap001files.storage.live.com/y4m5em9u4C4AK4Zq4xj_xPQncALHoYGfKtzIX7pQ_XTVcWDw1VMXuo4CBkjunytvkES1gInKMneJeKNp-CJTGK8A2OBYHaEx8XBiWH3YmNRWhvPQH55vxEaAQHFpHv2KasfP_HllD0FI4lmBe1xiwkuGjIvtX1LLtdJZkqNKvaXi445ucXPDeRepnjqWawG7H0D?width=1438&height=807&cropmode=none)
![Multiple scattering BSDF](https://dsm01pap001files.storage.live.com/y4meF_5cnv5W_T0_VwOSMlanmR2uehz33UIgqTEP3W5izlBWkpKt9HlaYb41dgq_F1A5SHYCWRBA7Tb4AIdeps2thElIEJxRIKOmc72pOEg8eg_-ucAq0R_wxuJJLqifTmCw2kK0_9LnrHd4NDAJMvJ7JeFoMU1XQChygwObAgpPUI4aLv2PxFTnCTmY9zqCP3E?width=1918&height=1032&cropmode=none)

