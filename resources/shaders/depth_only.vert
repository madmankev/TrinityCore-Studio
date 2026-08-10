#version 450 core
layout(location=0) in vec3 aPosition; layout(set=0,binding=0) uniform Shadow { mat4 lightViewProjection; mat4 model; } shadow; void main(){gl_Position=shadow.lightViewProjection*shadow.model*vec4(aPosition,1.0);}
