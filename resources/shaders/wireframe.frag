#version 450 core
layout(set=1,binding=0) uniform Wire { vec4 color; } wire; layout(location=0) out vec4 fragColor; void main(){ fragColor=wire.color; }
