#version 450 core
layout(location=0) in vec3 vWorldPos; layout(location=1) in vec2 vTexCoord;
layout(set=1,binding=0) uniform sampler2D uReflection; layout(set=1,binding=1) uniform sampler2D uFoam;
layout(set=1,binding=2) uniform WaterMaterial { vec4 uTint; float uOpacity; float uShoreBlend; } material;
layout(location=0) out vec4 fragColor;
void main(){ vec3 reflection=texture(uReflection,vTexCoord).rgb; float foam=texture(uFoam,vTexCoord*3.0).r*material.uShoreBlend; fragColor=vec4(mix(material.uTint.rgb,reflection,.35)+foam*.25,material.uTint.a*material.uOpacity); }
