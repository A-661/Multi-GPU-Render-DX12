#include "Common.hlsl"

struct VolumeData
{
    float3 BoxMinW;
    float Density;

    float3 BoxMaxW;
    float StepSize;

    float3 Color;
    float MaxOpacity;
};

ConstantBuffer<VolumeData> volumeBuffer : register(b2);
Texture2D<float> volumeDepthMap : register(t0, space2);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
};

float SafeReciprocal(float value)
{
    return abs(value) > 1e-6f ? rcp(value) : 1.0f;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    const float2 ndc = float2(
        uv.x * 2.0f - 1.0f,
        (1.0f - uv.y) * 2.0f - 1.0f);

    float4 worldPos = mul(float4(ndc, depth, 1.0f), worldBuffer.InvViewProj);
    worldPos *= SafeReciprocal(worldPos.w);

    return worldPos.xyz;
}

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);

    return frac((p.x + p.y) * p.z);
}

float Noise3D(float3 p)
{
    const float3 cell = floor(p);

    float3 local = frac(p);
    local = local * local * (3.0f - 2.0f * local);

    const float n000 = Hash31(cell + float3(0.0f, 0.0f, 0.0f));
    const float n100 = Hash31(cell + float3(1.0f, 0.0f, 0.0f));
    const float n010 = Hash31(cell + float3(0.0f, 1.0f, 0.0f));
    const float n110 = Hash31(cell + float3(1.0f, 1.0f, 0.0f));
    const float n001 = Hash31(cell + float3(0.0f, 0.0f, 1.0f));
    const float n101 = Hash31(cell + float3(1.0f, 0.0f, 1.0f));
    const float n011 = Hash31(cell + float3(0.0f, 1.0f, 1.0f));
    const float n111 = Hash31(cell + float3(1.0f, 1.0f, 1.0f));

    const float x00 = lerp(n000, n100, local.x);
    const float x10 = lerp(n010, n110, local.x);
    const float x01 = lerp(n001, n101, local.x);
    const float x11 = lerp(n011, n111, local.x);

    const float y0 = lerp(x00, x10, local.y);
    const float y1 = lerp(x01, x11, local.y);

    return lerp(y0, y1, local.z);
}

float FBM(float3 p)
{
    float value = 0.0f;
    float amplitude = 0.5f;

    [unroll]
    for (int octave = 0; octave < 5; ++octave)
    {
        value += Noise3D(p) * amplitude;
        p = p * 2.02f + float3(17.1f, 11.7f, 7.3f);
        amplitude *= 0.5f;
    }

    return value;
}

float SphereDensity(float3 p, float3 center, float radius)
{
    const float distanceToCenter = length(p - center);

    return 1.0f - smoothstep(radius * 0.72f, radius, distanceToCenter);
}

float SphereField(float3 p, float3 center, float radius)
{
    const float d = length(p - center);

    return saturate(1.0f - d / radius);
}

float RidgedNoise(float3 p)
{
    const float n = FBM(p);
    return 1.0f - abs(n * 2.0f - 1.0f);
}

float GetCloudDensity(float3 worldPosition)
{
    const float3 boxSize = max(
        volumeBuffer.BoxMaxW - volumeBuffer.BoxMinW,
        float3(1e-4f, 1e-4f, 1e-4f));

    const float3 uvw = (worldPosition - volumeBuffer.BoxMinW) / boxSize;

    if (any(uvw < 0.0f) || any(uvw > 1.0f))
    {
        return 0.0f;
    }

    const float fadeX =
        smoothstep(0.0f, 0.035f, uvw.x) *
        smoothstep(0.0f, 0.035f, 1.0f - uvw.x);

    const float fadeY =
        smoothstep(0.0f, 0.050f, uvw.y) *
        smoothstep(0.0f, 0.050f, 1.0f - uvw.y);

    const float fadeZ =
        smoothstep(0.0f, 0.035f, uvw.z) *
        smoothstep(0.0f, 0.035f, 1.0f - uvw.z);

    const float borderFade = fadeX * fadeY * fadeZ;

    const float3 windOffset = float3(
        worldBuffer.TotalTime * 0.20f,
        0.0f,
        worldBuffer.TotalTime * 0.08f);

    const float3 warp =
        float3(
            FBM((worldPosition + float3(17.0f, 3.0f, 11.0f) + windOffset) * 0.020f),
            FBM((worldPosition + float3(5.0f, 23.0f, 7.0f) + windOffset) * 0.020f),
            FBM((worldPosition + float3(13.0f, 9.0f, 29.0f) + windOffset) * 0.020f)) - 0.5f;

    const float3 warpedPosition = worldPosition + warp * 14.0f;

    const float macroA = FBM((warpedPosition + windOffset) * 0.012f);
    const float macroB = RidgedNoise((warpedPosition - windOffset * 0.7f) * 0.017f + float3(11.0f, 5.0f, 19.0f));
    const float macroC = FBM((warpedPosition + windOffset * 1.3f) * 0.026f + float3(23.0f, 7.0f, 31.0f));
    float field = 0.0f;
    field += macroA * 0.55f;
    field += macroB * 0.30f;
    field += macroC * 0.15f;

    const float centerBias =
        1.0f - saturate(length((uvw - 0.5f) * float3(1.10f, 0.85f, 1.10f)) * 1.15f);

    field += centerBias * 0.10f;
    
    const float detail = FBM(warpedPosition * 0.060f + float3(37.0f, 13.0f, 41.0f));

    float body = smoothstep(0.69f, 0.715f, field);
    
    body *= lerp(0.82f, 1.10f, detail);
    
    body = pow(saturate(body), 0.18f);
    const float backgroundMist = borderFade * lerp(0.0005f, 0.0025f, macroA);

    float density = backgroundMist + body * 0.99f;
    density *= borderFade;

    return saturate(density);
}

bool IntersectBox(
    float3 rayOrigin,
    float3 invRayDirection,
    out float tNear,
    out float tFar)
{
    const float3 t0 = (volumeBuffer.BoxMinW - rayOrigin) * invRayDirection;
    const float3 t1 = (volumeBuffer.BoxMaxW - rayOrigin) * invRayDirection;

    const float3 tMin = min(t0, t1);
    const float3 tMax = max(t0, t1);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);

    return tFar >= max(tNear, 0.0f);
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    const float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);

    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, worldBuffer.ViewProj);

    return vout;
}



float4 PS(VertexOut pin) : SV_Target
{
    const int2 pixelCoord = int2(pin.PosH.xy);
    const float2 uv = pin.PosH.xy * worldBuffer.InvRenderTargetSize;

    const float sceneDepth = volumeDepthMap.Load(int3(pixelCoord, 0));

    const float3 farPosW = ReconstructWorldPosition(uv, 1.0f);
    const float3 rayVector = farPosW - worldBuffer.EyePosW;

    const float farDistance = max(length(rayVector), 1e-4f);
    const float3 rayDirection = rayVector / farDistance;

    float sceneDistance = farDistance;

    if (sceneDepth < 1.0f - 1e-5f)
    {
        const float3 scenePosW = ReconstructWorldPosition(uv, sceneDepth);
        sceneDistance = max(length(scenePosW - worldBuffer.EyePosW), 1e-4f);
    }

    const float3 raySign = float3(
        rayDirection.x < 0.0f ? -1.0f : 1.0f,
        rayDirection.y < 0.0f ? -1.0f : 1.0f,
        rayDirection.z < 0.0f ? -1.0f : 1.0f);

    const float3 safeRayDirection = raySign * max(abs(rayDirection), 1e-5f);
    const float3 invRayDirection = rcp(safeRayDirection);

    float tNear;
    float tFar;

    if (!IntersectBox(worldBuffer.EyePosW, invRayDirection, tNear, tFar))
    {
        discard;
    }

    const float rayStart = max(tNear, 0.0f);
    const float rayEnd = min(tFar, sceneDistance);

    if (rayEnd <= rayStart)
    {
        discard;
    }

    const float stepLength = max(volumeBuffer.StepSize, 0.25f);

    float transmittance = 1.0f;
    float3 accumulatedColor = 0.0f;
    float t = rayStart + stepLength * 0.5f;

    [loop]
    for (int stepIndex = 0; stepIndex < 192; ++stepIndex)
    {
        if (t >= rayEnd)
        {
            break;
        }

        const float3 samplePosition = worldBuffer.EyePosW + rayDirection * t;
        const float densityField = GetCloudDensity(samplePosition);

        if (densityField > 1e-4f)
        {
            const float localDensity = densityField * max(volumeBuffer.Density, 0.0f);
            const float sampleOpacity = 1.0f - exp(-localDensity * stepLength);

            const float brightness = lerp(1.12f, 0.60f, densityField);

            const float height01 = saturate(
                (samplePosition.y - volumeBuffer.BoxMinW.y) /
                max(volumeBuffer.BoxMaxW.y - volumeBuffer.BoxMinW.y, 1e-4f));

            const float heightLighting = lerp(0.92f, 1.06f, height01);
            const float3 sampleColor = volumeBuffer.Color * brightness * heightLighting;

            accumulatedColor += transmittance * sampleOpacity * sampleColor;
            transmittance *= 1.0f - sampleOpacity;

            const float currentOpacity = 1.0f - transmittance;

            if (currentOpacity >= saturate(volumeBuffer.MaxOpacity))
            {
                break;
            }

            if (transmittance < 0.01f)
            {
                break;
            }
        }

        t += stepLength;
    }

    float opacity = 1.0f - transmittance;
    const float maxOpacity = saturate(volumeBuffer.MaxOpacity);

    if (opacity > maxOpacity)
    {
        const float opacityScale = maxOpacity / max(opacity, 1e-5f);
        accumulatedColor *= opacityScale;
        opacity = maxOpacity;
    }

    if (opacity < 1e-4f)
    {
        discard;
    }

    return float4(accumulatedColor, opacity);
}

Texture2D<float4> volumeCompositeMap : register(t0, space3);

struct CompositeVertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

CompositeVertexOut VSComposite(uint vertexID : SV_VertexID)
{
    CompositeVertexOut output;

    const float2 texCoord = float2(
        (vertexID << 1) & 2,
        vertexID & 2);

    output.TexC = texCoord;

    output.PosH = float4(
        texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f,
        1.0f);

    return output;
}

float4 PSComposite(CompositeVertexOut pin) : SV_Target
{
    return volumeCompositeMap.Sample(gsamLinearClamp, pin.TexC);
}