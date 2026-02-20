#version 330 core
in vec2 vUV;
in float vSky;
in float vBlock;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uAtlas;
uniform int uRenderMode; // 0 = textured, 1 = flat
uniform int uAlphaPass;  // 0 = opaque, 1 = transparent, 2 = all
uniform vec3 uSkyTint;
uniform vec3 uCelestialDir;
uniform float uCelestialStrength;
uniform vec3 uPlayerPos;
uniform float uHeldTorchStrength;
uniform vec3 uFogColor;
uniform float uFogNear;
uniform float uFogFar;
uniform float uCloudShadowEnabled;
uniform float uCloudShadowTime;
uniform float uCloudShadowStrength;
uniform float uCloudShadowDay;
uniform float uCloudLayerY;
uniform float uCloudShadowRange;

out vec4 FragColor;

uint cloudHashU32(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float hashCloudCell(ivec2 c, int salt) {
    uint x = uint(c.x);
    uint z = uint(c.y);
    uint s = uint(salt);
    uint h = cloudHashU32(x ^ (cloudHashU32(z + 0x9e3779b9u) + s * 0x85ebca6bu));
    return float(h & 0x00ffffffu) * (1.0 / 16777215.0);
}

bool cloudCellFilled(ivec2 f) {
    float base = hashCloudCell(f, 503);
    float nE = hashCloudCell(f + ivec2(1, 0), 503);
    float nW = hashCloudCell(f + ivec2(-1, 0), 503);
    float nN = hashCloudCell(f + ivec2(0, 1), 503);
    float nS = hashCloudCell(f + ivec2(0, -1), 503);
    float neighborMax = max(max(nE, nW), max(nN, nS));
    bool core = base > 0.77;
    bool fringe = (base > 0.69) && (neighborMax > 0.78);
    return core || fringe;
}

bool cloudTileFilled(ivec2 g) {
    ivec2 f = ivec2(floor(vec2(g) * 0.5));
    return cloudCellFilled(f);
}

float cloudCoverage(vec2 worldXZ, float t) {
    const float cell = 16.0;
    float driftX = t * 0.35;
    float driftZ = t * 0.12;
    vec2 gridPos = (worldXZ - vec2(driftX, driftZ)) / cell;
    // Cloud tiles are centered at integer grid points; sample nearest center-aligned tile.
    ivec2 g = ivec2(floor(gridPos + 0.5));
    if (!cloudTileFilled(g)) {
        return 0.0;
    }

    bool eastFilled = cloudTileFilled(g + ivec2(1, 0));
    bool westFilled = cloudTileFilled(g + ivec2(-1, 0));
    bool northFilled = cloudTileFilled(g + ivec2(0, 1));
    bool southFilled = cloudTileFilled(g + ivec2(0, -1));

    vec2 delta = gridPos - vec2(g);
    float dE = 0.5 - delta.x;
    float dW = 0.5 + delta.x;
    float dN = 0.5 - delta.y;
    float dS = 0.5 + delta.y;
    const float feather = 0.10;

    float edgeBlend = 1.0;
    if (!eastFilled) {
        edgeBlend *= smoothstep(0.0, feather, dE);
    }
    if (!westFilled) {
        edgeBlend *= smoothstep(0.0, feather, dW);
    }
    if (!northFilled) {
        edgeBlend *= smoothstep(0.0, feather, dN);
    }
    if (!southFilled) {
        edgeBlend *= smoothstep(0.0, feather, dS);
    }
    return edgeBlend;
}

void main() {
    vec3 lightDir = normalize(uCelestialDir);
    float ndl = max(dot(normalize(vNormal), lightDir), 0.0);
    float wrapped = ndl * 0.85 + 0.15;
    float directional = mix(1.0, wrapped, clamp(uCelestialStrength, 0.0, 1.0));
    float skyLit = clamp(vSky * directional, 0.0, 1.0);
    float heldDist = length(vWorldPos - uPlayerPos);
    float heldLight = clamp(1.0 - heldDist / 10.0, 0.0, 1.0);
    heldLight = heldLight * heldLight * clamp(uHeldTorchStrength, 0.0, 1.0);
    float fogT = smoothstep(uFogNear, uFogFar, heldDist);
    // Slightly curved distance fog feels less linear and hides chunk-edge pop better.
    fogT = pow(clamp(fogT, 0.0, 1.0), 1.35);

    float cloudShadow = 1.0;
    float cloudDeltaY = (uCloudLayerY - vWorldPos.y);
    if (uCloudShadowEnabled > 0.5 && cloudDeltaY > 0.0 && lightDir.y > 0.005) {
        float invY = 1.0 / max(lightDir.y, 0.005);
        vec2 sampleXZ = vWorldPos.xz + (lightDir.xz * (cloudDeltaY * invY));
        vec2 d = abs(sampleXZ - uPlayerPos.xz);
        if (max(d.x, d.y) <= (uCloudShadowRange + 8.0)) {
            float cov = cloudCoverage(sampleXZ, uCloudShadowTime);
            float str = clamp(uCloudShadowStrength * uCloudShadowDay, 0.0, 1.0);
            cloudShadow = 1.0 - cov * str;
        }
    }
    skyLit *= cloudShadow;

    if (uRenderMode == 0) {
        vec4 texel = texture(uAtlas, vUV);
        bool isTransparent = texel.a < 0.99;
        if (uAlphaPass == 0 && isTransparent) {
            discard;
        }
        if (uAlphaPass == 1 && !isTransparent) {
            discard;
        }
        vec3 skyTerm = uSkyTint * skyLit;
        vec3 blockTerm = vec3(max(vBlock, heldLight));
        vec3 lightTerm = clamp(max(skyTerm, blockTerm), 0.0, 1.0);
        lightTerm = max(lightTerm, vec3(0.035));
        vec3 lit = texel.rgb * lightTerm;
        lit = mix(lit, uFogColor, fogT);
        FragColor = vec4(lit, texel.a);
    } else {
        if (uAlphaPass != 2) {
            // Flat mode does not currently separate transparent materials.
            discard;
        }
        // Flat mesh visualization: shading only, no block texture lookup.
        vec3 skyTerm = uSkyTint * skyLit;
        vec3 blockTerm = vec3(max(vBlock, heldLight));
        vec3 lightTerm = clamp(max(skyTerm, blockTerm), 0.0, 1.0);
        lightTerm = max(lightTerm, vec3(0.035));
        vec3 lit = mix(lightTerm, uFogColor, fogT);
        FragColor = vec4(lit, 1.0);
    }
}
