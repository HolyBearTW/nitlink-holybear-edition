#pragma once

namespace NitLink {
// Blur the displayed picture in linear light, including the HDR10 backbuffer.
// The input is a copy of the panel region; capture textures remain untouched.
inline constexpr char kPanelGlassShader[] = R"hlsl(
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Glass : register(b7) {
    float2 sourceSize;
    uint hdr;
    float radius;
    float4 region;
};
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2,-2) + float2(-1,1),0,1);
    return output;
}
float3 Decode(float3 value) {
    if(hdr) {
        float3 p = pow(max(value,0),1.0/78.84375);
        return pow(max((p-0.8359375)/(18.8515625-18.6875*p),0),1.0/0.1593017578125);
    }
    return lerp(value/12.92,pow(max((value+0.055)/1.055,0),2.4),step(0.04045,value));
}
float3 Encode(float3 value) {
    value=max(value,0);
    if(hdr) {
        float3 p=pow(value,0.1593017578125);
        return pow((0.8359375+18.8515625*p)/(1+18.6875*p),78.84375);
    }
    return lerp(value*12.92,1.055*pow(value,1.0/2.4)-0.055,step(0.0031308,value));
}
float4 Horizontal(Vertex input) : SV_TARGET {
    static const float weights[5]={0.227027027,0.194594595,0.121621622,0.054054054,0.016216216};
    int2 pixel=int2(input.uv*sourceSize);
    float3 result=Decode(picture.Load(int3(clamp(pixel,int2(0,0),int2(sourceSize)-1),0)).rgb)*weights[0];
    [unroll] for(int i=1;i<5;++i) {
        int2 delta=int2(round(i*radius),0);
        result+=(Decode(picture.Load(int3(clamp(pixel+delta,int2(0,0),int2(sourceSize)-1),0)).rgb)
               + Decode(picture.Load(int3(clamp(pixel-delta,int2(0,0),int2(sourceSize)-1),0)).rgb))*weights[i];
    }
    return float4(result,1);
}
float4 Vertical(Vertex input) : SV_TARGET {
    static const float weights[5]={0.227027027,0.194594595,0.121621622,0.054054054,0.016216216};
    float2 uv=region.xy+input.uv*region.zw;
    float3 result=picture.SampleLevel(linearClamp,uv,0).rgb*weights[0];
    [unroll] for(int i=1;i<5;++i) {
        float2 delta=float2(0,i*radius/sourceSize.y);
        result+=(picture.SampleLevel(linearClamp,uv+delta,0).rgb+picture.SampleLevel(linearClamp,uv-delta,0).rgb)*weights[i];
    }
    return float4(Encode(result),1);
}
)hlsl";
}
