#version 450 core
layout(location=0) in vec4 vColor; layout(set=1,binding=0) uniform sampler2D uParticle; layout(location=0) out vec4 fragColor; void main(){ fragColor=vColor*texture(uParticle,gl_PointCoord); if(fragColor.a<.01) discard; }
