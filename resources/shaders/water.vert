#version 450 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec2 aTexCoord;
layout(set=0,binding=0) uniform WaterCamera { mat4 uView; mat4 uProjection; float uTime; float uWaveHeight; float uWaveSpeed; } camera;
layout(location=0) out vec3 vWorldPos; layout(location=1) out vec2 vTexCoord;
void main(){ vec3 p=aPosition; p.y+=sin(p.x*0.18+camera.uTime*camera.uWaveSpeed)*camera.uWaveHeight+cos(p.z*0.12+camera.uTime*camera.uWaveSpeed*.7)*camera.uWaveHeight*.5; vWorldPos=p; vTexCoord=aTexCoord; gl_Position=camera.uProjection*camera.uView*vec4(p,1.0); }
