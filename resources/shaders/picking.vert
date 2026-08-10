#version 450 core
layout(location=0) in vec3 aPosition; layout(set=0,binding=0) uniform Pick { mat4 view; mat4 projection; mat4 model; uint objectId; } pick; layout(location=0) flat out uint vObjectId; void main(){ vObjectId=pick.objectId; gl_Position=pick.projection*pick.view*pick.model*vec4(aPosition,1.0); }
