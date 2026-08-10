#version 450 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec3 aNormal; layout(set=0,binding=0) uniform Scene { mat4 view; mat4 projection; mat4 model; float width; } scene; void main(){ vec4 world=scene.model*vec4(aPosition+normalize(aNormal)*scene.width,1.0); gl_Position=scene.projection*scene.view*world; }
