#version 450 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aTexCoord;
layout(location=3) in uvec4 aBoneIndices; layout(location=4) in vec4 aBoneWeights;
layout(set=0,binding=0) uniform Scene { mat4 uView; mat4 uProjection; mat4 uModel; } scene;
layout(set=0,binding=1) uniform Bones { mat4 uBones[256]; } bones;
layout(location=0) out vec3 vWorldPos; layout(location=1) out vec3 vNormal; layout(location=2) out vec2 vTexCoord;
void main(){ mat4 skin=aBoneWeights.x*bones.uBones[aBoneIndices.x]+aBoneWeights.y*bones.uBones[aBoneIndices.y]+aBoneWeights.z*bones.uBones[aBoneIndices.z]+aBoneWeights.w*bones.uBones[aBoneIndices.w]; vec4 local=skin*vec4(aPosition,1.0); vec4 world=scene.uModel*local; vWorldPos=world.xyz; vNormal=mat3(scene.uModel*skin)*aNormal; vTexCoord=aTexCoord; gl_Position=scene.uProjection*scene.uView*world; }
