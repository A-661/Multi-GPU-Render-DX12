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

    gFogOutput[dispatchThreadId.xy] = 0.0f;
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
