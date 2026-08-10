#version 450 core
layout(location=0) in vec3 aPosition; layout(set=0,binding=0) uniform Camera { mat4 view; mat4 projection; } camera; layout(location=0) out vec3 vDirection; void main(){ vDirection=aPosition; vec4 p=camera.projection*mat4(mat3(camera.view))*vec4(aPosition,1.0); gl_Position=p.xyww; }
