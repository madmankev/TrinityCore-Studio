#version 450 core
layout(location=0) in vec3 vWorldPos;
layout(location=1) in vec3 vNormal;
layout(location=2) in vec2 vTexCoord;
layout(location=3) in vec4 vVertexColor;
layout(location=4) in float vHeight;
layout(set=1,binding=0) uniform sampler2DArray uTextureArray;
layout(set=1,binding=1) uniform sampler2D uSplatmap;
layout(set=1,binding=2) uniform sampler2D uDetailTextures[4];
layout(set=1,binding=6) uniform LayerSettings { vec4 uLayerSettings[4]; } layers;
layout(location=0) out vec4 fragColor;
void main() {
 vec4 blendWeights=texture(uSplatmap,vTexCoord); float totalWeight=dot(blendWeights,vec4(1.0)); blendWeights/=max(totalWeight,0.001);
 vec4 color0=texture(uTextureArray,vec3(vTexCoord*layers.uLayerSettings[0].y,layers.uLayerSettings[0].x));
 vec4 color1=texture(uTextureArray,vec3(vTexCoord*layers.uLayerSettings[1].y,layers.uLayerSettings[1].x));
 vec4 color2=texture(uTextureArray,vec3(vTexCoord*layers.uLayerSettings[2].y,layers.uLayerSettings[2].x));
 vec4 color3=texture(uTextureArray,vec3(vTexCoord*layers.uLayerSettings[3].y,layers.uLayerSettings[3].x));
 vec4 finalColor=color0*blendWeights.r+color1*blendWeights.g+color2*blendWeights.b+color3*blendWeights.a;
 finalColor.rgb*=vVertexColor.rgb; vec3 lightDir=normalize(vec3(-0.5,-0.8,-0.3)); float diffuse=max(dot(normalize(vNormal),lightDir),0.0); finalColor.rgb*=0.3+0.7*diffuse; fragColor=finalColor;
}
