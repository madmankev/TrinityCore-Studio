#version 450 core
layout(location=0) in vec3 vDirection; layout(set=1,binding=0) uniform samplerCube uSkybox; layout(location=0) out vec4 fragColor; void main(){ fragColor=texture(uSkybox,normalize(vDirection)); }
