cbuffer cbFog : register(b0)
{
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4 gResolution;

    float4 gFogColor;

    float3 gCameraPosW;
    float gDensity;

    float gHeightFalloff;
    float gFogBaseHeight;
    float gStartDistance;
    float gMaxOpacity;

    float gNearZ;
    float gFarZ;
    float2 gPadding;
};

Texture2D<float> gInputDepth : register(t0);
RWTexture2D<float> gFogOutput : register(u0);

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gFogMap : register(t1);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);

float SafeReciprocal(float value)
{
    return (abs(value) > 1e-6f) ? rcp(value) : 1.0f;
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - uv.y) * 2.0f - 1.0f;

    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 viewPos = mul(clipPos, gInvProj);
    viewPos *= SafeReciprocal(viewPos.w);

    float4 worldPos = mul(viewPos, gInvView);
    return worldPos.xyz;
}

float ComputeFogDensity(float height)
{
    const float relativeHeight = height - gFogBaseHeight;
    return gDensity * exp(-max(gHeightFalloff, 1e-6f) * relativeHeight);
}

float IntegrateHeightFogRayMarch(float3 rayDir, float rayStart, float rayEnd, float jitter)
{
    if (rayEnd <= rayStart)
    {
        return 0.0f;
    }

    const uint stepCount = 24;
    const float rayLength = rayEnd - rayStart;
    const float stepSize = rayLength / stepCount;

    float transmittance = 1.0f;
    float fogFactor = 0.0f;

    [loop]
    for (uint i = 0; i < stepCount; ++i)
    {
        const float stepT = rayStart + stepSize * (i + jitter);
        const float sampleHeight = gCameraPosW.y + rayDir.y * stepT;
        const float density = ComputeFogDensity(sampleHeight);
        const float localTau = density * stepSize;
        const float localTransmittance = exp(-localTau);
        const float localFog = 1.0f - localTransmittance;

        fogFactor += transmittance * localFog;
        transmittance *= localTransmittance;
    }

    return min(fogFactor, gMaxOpacity);
}

float ComputeFogAtUv(float2 uv, float2 pixelCoord, float jitter)
{
    const float depth = gInputDepth.SampleLevel(gsamLinearClamp, uv, 0.0f);
    const float3 farWorldPos = ReconstructWorldPosition(uv, 1.0f);
    const float3 rayVector = farWorldPos - gCameraPosW;
    const float rayVectorLength = max(length(rayVector), 1e-6f);
    const float3 rayDir = rayVector / rayVectorLength;

    float maxDistance = rayVectorLength;
    if (depth < 1.0f - 1e-5f)
    {
        const float3 worldPos = ReconstructWorldPosition(uv, depth);
        maxDistance = max(length(worldPos - gCameraPosW), 1e-3f);
    }

    const float rayStart = min(gStartDistance, maxDistance);
    const float sampleJitter = frac(jitter + InterleavedGradientNoise(pixelCoord));

    return saturate(IntegrateHeightFogRayMarch(rayDir, rayStart, maxDistance, sampleJitter));
}

[numthreads(32, 32, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gFogOutput.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float2 pixelCoord = float2(dispatchThreadId.xy);
    const float2 baseUv = (pixelCoord + 0.5f) * gResolution.zw;

    const float2 subPixelOffsets[4] =
    {
        float2(-0.25f, -0.25f),
        float2( 0.25f, -0.25f),
        float2(-0.25f,  0.25f),
        float2( 0.25f,  0.25f)
    };

    float fogMask = 0.0f;

    [unroll]
    for (uint sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
    {
        const float2 sampleUv = saturate(baseUv + subPixelOffsets[sampleIndex] * gResolution.zw);
        const float jitter = (sampleIndex + 0.5f) * 0.25f;
        fogMask += ComputeFogAtUv(sampleUv, pixelCoord + subPixelOffsets[sampleIndex], jitter);
    }

    fogMask *= 0.25f;

    gFogOutput[dispatchThreadId.xy] = saturate(fogMask);
}

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VSComposite(uint vertexID : SV_VertexID)
{
    VSOut output;

    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.TexC = texCoord;
    output.PosH = float4(texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    return output;
}

float4 PSComposite(VSOut pin) : SV_Target
{
    float4 sceneColor = gSceneColor.Sample(gsamLinearClamp, pin.TexC);
    float fogFactor = saturate(gFogMap.Sample(gsamLinearClamp, pin.TexC));

    return lerp(sceneColor, gFogColor, fogFactor);
}
