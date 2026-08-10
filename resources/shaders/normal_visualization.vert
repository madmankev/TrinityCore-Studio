#version 450 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec3 aNormal; layout(set=0,binding=0) uniform Scene { mat4 view; mat4 projection; mat4 model; float length; } scene; void main(){ vec3 p=aPosition+normalize(aNormal)*scene.length; gl_Position=scene.projection*scene.view*scene.model*vec4(p,1.0); }
