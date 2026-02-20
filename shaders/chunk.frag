#version 330 core
in vec2 vUV;
in float vLight;

uniform sampler2D uAtlas;
uniform int uRenderMode; // 0 = textured, 1 = flat
uniform int uAlphaPass;  // 0 = opaque, 1 = transparent, 2 = all

out vec4 FragColor;

void main() {
    if (uRenderMode == 0) {
        vec4 texel = texture(uAtlas, vUV);
        bool isTransparent = texel.a < 0.99;
        if (uAlphaPass == 0 && isTransparent) {
            discard;
        }
        if (uAlphaPass == 1 && !isTransparent) {
            discard;
        }
        FragColor = vec4(texel.rgb * vLight, texel.a);
    } else {
        if (uAlphaPass != 2) {
            // Flat mode does not currently separate transparent materials.
            discard;
        }
        // Flat mesh visualization: shading only, no block texture lookup.
        FragColor = vec4(vec3(vLight), 1.0);
    }
}
