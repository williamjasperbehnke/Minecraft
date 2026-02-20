#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aSkyLight;
layout(location = 4) in float aBlockLight;

uniform mat4 uProj;
uniform mat4 uView;
uniform float uDaylight;

out vec2 vUV;
out float vSky;
out float vBlock;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    float day = clamp(uDaylight, 0.0, 1.0);
    vSky = clamp(aSkyLight * mix(0.05, 1.0, day), 0.0, 1.0);
    vBlock = clamp(aBlockLight, 0.0, 1.0);
    vNormal = normalize(aNormal);
    vWorldPos = aPos;
    vUV = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
