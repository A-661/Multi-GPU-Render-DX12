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

float IntegrateHeightFog(float3 rayDir, float rayStart, float rayEnd)
{
    if (rayEnd <= rayStart)
    {
        return 0.0f;
    }

    const float lambda = max(gHeightFalloff, 1e-6f);
    const float baseHeightOffset = gCameraPosW.y - gFogBaseHeight;
    const float segmentLength = rayEnd - rayStart;

    if (abs(rayDir.y) < 1e-4f)
    {
        const float density = gDensity * exp(-lambda * (baseHeightOffset + rayDir.y * rayStart));
        return density * segmentLength;
    }

    const float y0 = baseHeightOffset + rayDir.y * rayStart;
    const float y1 = baseHeightOffset + rayDir.y * rayEnd;
    const float exp0 = exp(-lambda * y0);
    const float exp1 = exp(-lambda * y1);

    return abs((gDensity / (lambda * rayDir.y)) * (exp0 - exp1));
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

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) * gResolution.zw;
    const float depth = gInputDepth.Load(int3(dispatchThreadId.xy, 0));

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
    float tau = IntegrateHeightFog(rayDir, rayStart, maxDistance);
    tau = clamp(tau, 0.0f, 50.0f);

    const float transmittance = exp(-tau);
    const float fogMask = min(1.0f - transmittance, gMaxOpacity);

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
