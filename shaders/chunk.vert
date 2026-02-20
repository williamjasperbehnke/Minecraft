#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uProj;
uniform mat4 uView;

out vec2 vUV;
out float vLight;

void main() {
    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.5));
    vLight = clamp(dot(normalize(aNormal), lightDir), 0.2, 1.0);
    vUV = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
