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


ConstantBuffer<VolumeData> volumeBuffer
    : register(b2);

Texture2D<float> volumeDepthMap
    : register(t0, space2);


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
    return
        (abs(value) > 1e-6f)
        ? rcp(value)
        : 1.0f;
}


float3 ReconstructWorldPosition(
    float2 uv,
    float depth)
{
    const float2 ndc = float2(
        uv.x * 2.0f - 1.0f,
        (1.0f - uv.y) * 2.0f - 1.0f);

    float4 worldPos =
        mul(
            float4(ndc, depth, 1.0f),
            worldBuffer.InvViewProj);

    worldPos *=
        SafeReciprocal(worldPos.w);

    return worldPos.xyz;
}


bool IntersectBox(
    float3 rayOrigin,
    float3 invRayDirection,
    out float tNear,
    out float tFar)
{
    const float3 t0 =
        (volumeBuffer.BoxMinW - rayOrigin)
        * invRayDirection;

    const float3 t1 =
        (volumeBuffer.BoxMaxW - rayOrigin)
        * invRayDirection;

    const float3 tMin =
        min(t0, t1);

    const float3 tMax =
        max(t0, t1);

    tNear =
        max(
            max(tMin.x, tMin.y),
            tMin.z);

    tFar =
        min(
            min(tMax.x, tMax.y),
            tMax.z);

    return
        tFar >= max(tNear, 0.0f);
}


VertexOut VS(VertexIn vin)
{
    VertexOut vout =
        (VertexOut)0.0f;

    const float4 posW =
        mul(
            float4(vin.PosL, 1.0f),
            objectBuffer.World);

    vout.PosW = posW.xyz;

    vout.PosH =
        mul(
            posW,
            worldBuffer.ViewProj);

    return vout;
}


float4 PS(VertexOut pin) : SV_Target
{
    // Current screen pixel.
    const int2 pixelCoord =
        int2(pin.PosH.xy);

    const float2 uv =
        pin.PosH.xy
        * worldBuffer.InvRenderTargetSize;


    // Exact depth value of this pixel.
    const float sceneDepth =
        volumeDepthMap.Load(
            int3(pixelCoord, 0));


    // --------------------------------------------------------
    // Camera ray
    // --------------------------------------------------------

    const float3 farPosW =
        ReconstructWorldPosition(
            uv,
            1.0f);

    const float3 rayVector =
        farPosW -
        worldBuffer.EyePosW;

    const float farDistance =
        max(
            length(rayVector),
            1e-4f);

    const float3 rayDirection =
        rayVector / farDistance;


    // --------------------------------------------------------
    // Distance to opaque geometry
    // --------------------------------------------------------

    float sceneDistance =
        farDistance;

    if (sceneDepth < 1.0f - 1e-5f)
    {
        const float3 scenePosW =
            ReconstructWorldPosition(
                uv,
                sceneDepth);

        sceneDistance =
            max(
                length(
                    scenePosW -
                    worldBuffer.EyePosW),
                1e-4f);
    }


    // --------------------------------------------------------
    // Safe inverse ray direction
    // --------------------------------------------------------

    const float3 raySign =
        float3(
            rayDirection.x < 0.0f
                ? -1.0f : 1.0f,

            rayDirection.y < 0.0f
                ? -1.0f : 1.0f,

            rayDirection.z < 0.0f
                ? -1.0f : 1.0f);


    const float3 safeRayDirection =
        raySign *
        max(
            abs(rayDirection),
            1e-5f);

    const float3 invRayDirection =
        rcp(safeRayDirection);


    // --------------------------------------------------------
    // Ray / volume intersection
    // --------------------------------------------------------

    float tNear;
    float tFar;

    if (!IntersectBox(
            worldBuffer.EyePosW,
            invRayDirection,
            tNear,
            tFar))
    {
        discard;
    }


    const float rayStart =
        max(tNear, 0.0f);

    const float rayEnd =
        min(tFar, sceneDistance);

    if (rayEnd <= rayStart)
    {
        discard;
    }


    // --------------------------------------------------------
    // Homogeneous volume
    // --------------------------------------------------------

    const float segmentLength =
        rayEnd - rayStart;

    const float density =
        max(
            volumeBuffer.Density,
            0.0f);

    const float opacity =
        min(
            1.0f -
            exp(
                -density *
                segmentLength),

            saturate(
                volumeBuffer.MaxOpacity));


    // Premultiplied alpha.
    return float4(
        volumeBuffer.Color * opacity,
        opacity);
}

Texture2D<float4> volumeCompositeMap
    : register(t0, space3);

struct CompositeVertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

CompositeVertexOut VSComposite(uint vertexID : SV_VertexID)
{
    CompositeVertexOut output;

    const float2 texCoord =
        float2(
            (vertexID << 1) & 2,
            vertexID & 2);

    output.TexC = texCoord;

    output.PosH =
        float4(
            texCoord *
            float2(2.0f, -2.0f) +
            float2(-1.0f, 1.0f),
            0.0f,
            1.0f);

    return output;
}

float4 PSComposite(
    CompositeVertexOut pin) : SV_Target
{
    return volumeCompositeMap.Sample(
        gsamLinearClamp,
        pin.TexC);
}