#version 450 core
layout(location=0) flat in uint vObjectId; layout(location=0) out uint objectId; void main(){ objectId=vObjectId; }
