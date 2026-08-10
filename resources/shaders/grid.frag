#version 450 core
layout(location=0) in vec3 vWorld; layout(set=1,binding=0) uniform Grid { float spacing; vec4 color; } grid; layout(location=0) out vec4 fragColor; void main(){ vec2 g=abs(fract(vWorld.xz/grid.spacing-.5)-.5)/fwidth(vWorld.xz/grid.spacing); float line=1.-min(min(g.x,g.y),1.); fragColor=vec4(grid.color.rgb,grid.color.a*line); }
