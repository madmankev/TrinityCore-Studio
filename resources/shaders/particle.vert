#version 450 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec4 aColor; layout(location=2) in float aSize; layout(set=0,binding=0) uniform Camera { mat4 view; mat4 projection; } camera; layout(location=0) out vec4 vColor; void main(){ vColor=aColor; gl_Position=camera.projection*camera.view*vec4(aPosition,1.0); gl_PointSize=aSize; }
