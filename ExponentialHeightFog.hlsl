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

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p)
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

    const float nx00 = lerp(n000, n100, local.x);
    const float nx10 = lerp(n010, n110, local.x);
    const float nx01 = lerp(n001, n101, local.x);
    const float nx11 = lerp(n011, n111, local.x);

    const float nxy0 = lerp(nx00, nx10, local.y);
    const float nxy1 = lerp(nx01, nx11, local.y);

    return lerp(nxy0, nxy1, local.z);
}

float FBM(float3 p)
{
    float sum = 0.0f;
    float amplitude = 0.5f;

    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        sum += amplitude * ValueNoise3D(p);
        p = p * 2.03f + 17.0f;
        amplitude *= 0.5f;
    }

    return sum;
}

float CloudHeightWeight(float y, float layerCenter, float layerHalfThickness)
{
    const float normalizedDistance = abs(y - layerCenter) * SafeReciprocal(max(layerHalfThickness, 1e-3f));
    float weight = saturate(1.0f - normalizedDistance);
    weight = weight * weight * (3.0f - 2.0f * weight);
    return weight * weight;
}

float CloudDensityAtPosition(float3 worldPos, float layerCenter, float layerHalfThickness)
{
    const float heightWeight = CloudHeightWeight(worldPos.y, layerCenter, layerHalfThickness);
    if (heightWeight <= 0.0f)
    {
        return 0.0f;
    }

    const float3 noisePos = worldPos * 0.0035f;
    const float shapeNoise = FBM(noisePos);
    const float detailNoise = ValueNoise3D(noisePos * 3.7f + 19.2f);

    const float coverage = saturate((shapeNoise - 0.48f) * 2.25f);
    const float detail = lerp(0.75f, 1.15f, detailNoise);

    return gDensity * heightWeight * coverage * detail;
}

bool IntersectCloudLayer(float3 rayOrigin, float3 rayDir, float layerCenter, float layerHalfThickness,
                         float rayStart, float rayEnd, out float hitStart, out float hitEnd)
{
    if (rayEnd <= rayStart)
    {
        hitStart = 0.0f;
        hitEnd = 0.0f;
        return false;
    }

    const float layerMin = layerCenter - layerHalfThickness;
    const float layerMax = layerCenter + layerHalfThickness;

    if (abs(rayDir.y) < 1e-4f)
    {
        if (rayOrigin.y < layerMin || rayOrigin.y > layerMax)
        {
            hitStart = 0.0f;
            hitEnd = 0.0f;
            return false;
        }

        hitStart = rayStart;
        hitEnd = rayEnd;
        return true;
    }

    float t0 = (layerMin - rayOrigin.y) / rayDir.y;
    float t1 = (layerMax - rayOrigin.y) / rayDir.y;

    if (t0 > t1)
    {
        const float temp = t0;
        t0 = t1;
        t1 = temp;
    }

    hitStart = max(rayStart, t0);
    hitEnd = min(rayEnd, t1);

    return hitEnd > hitStart;
}

float IntegrateCloudLayer(float3 rayOrigin, float3 rayDir, float rayStart, float rayEnd)
{
    const float layerCenter = gFogBaseHeight;
    const float layerHalfThickness = max(25.0f, 0.5f * SafeReciprocal(max(gHeightFalloff, 1e-3f)));

    float hitStart;
    float hitEnd;
    if (!IntersectCloudLayer(rayOrigin, rayDir, layerCenter, layerHalfThickness, rayStart, rayEnd, hitStart, hitEnd))
    {
        return 0.0f;
    }

    const float segmentLength = hitEnd - hitStart;
    const float3 samplePos0 = rayOrigin + rayDir * lerp(hitStart, hitEnd, 0.2f);
    const float3 samplePos1 = rayOrigin + rayDir * lerp(hitStart, hitEnd, 0.5f);
    const float3 samplePos2 = rayOrigin + rayDir * lerp(hitStart, hitEnd, 0.8f);

    const float density0 = CloudDensityAtPosition(samplePos0, layerCenter, layerHalfThickness);
    const float density1 = CloudDensityAtPosition(samplePos1, layerCenter, layerHalfThickness);
    const float density2 = CloudDensityAtPosition(samplePos2, layerCenter, layerHalfThickness);

    const float averageDensity = (density0 + density1 + density2) / 3.0f;

    return averageDensity * segmentLength;
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
    float tau = IntegrateCloudLayer(gCameraPosW, rayDir, rayStart, maxDistance);
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
