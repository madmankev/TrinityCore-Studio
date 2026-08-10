#version 450 core
layout(location=0) in vec3 aPosition; layout(set=0,binding=0) uniform Camera { mat4 view; mat4 projection; mat4 model; } camera; void main(){ gl_Position=camera.projection*camera.view*camera.model*vec4(aPosition,1.0); }
