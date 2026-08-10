#version 450 core
layout(set=1,binding=0) uniform Outline { vec4 color; } outline; layout(location=0) out vec4 fragColor; void main(){ fragColor=outline.color; }
