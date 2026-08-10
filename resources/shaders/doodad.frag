#version 450 core
layout(location=0) in vec3 vWorldPos; layout(location=1) in vec3 vNormal; layout(location=2) in vec2 vTexCoord;
layout(set=1,binding=0) uniform sampler2D uDiffuse; layout(set=1,binding=1) uniform sampler2D uNormal;
layout(location=0) out vec4 fragColor;
void main(){ vec4 albedo=texture(uDiffuse,vTexCoord); if(albedo.a<0.02) discard; vec3 n=normalize(vNormal); float light=max(dot(n,normalize(vec3(-0.5,-0.8,-0.3))),0.0); fragColor=vec4(albedo.rgb*(0.25+0.75*light),albedo.a); }
