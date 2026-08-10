#version 450 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;
layout(location=3) in vec4 aVertexColor;
layout(set=0,binding=0) uniform Camera { mat4 uView; mat4 uProjection; } camera;
layout(location=0) out vec3 vWorldPos;
layout(location=1) out vec3 vNormal;
layout(location=2) out vec2 vTexCoord;
layout(location=3) out vec4 vVertexColor;
layout(location=4) out float vHeight;
void main() { vWorldPos=aPosition; vNormal=normalize(aNormal); vTexCoord=aTexCoord; vVertexColor=aVertexColor; vHeight=aPosition.y; gl_Position=camera.uProjection*camera.uView*vec4(aPosition,1.0); }
